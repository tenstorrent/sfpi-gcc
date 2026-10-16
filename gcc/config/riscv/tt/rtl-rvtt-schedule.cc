/* Pass to schedule tensix insns (insert nops)
   Copyright (C) 2022-2026 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).
   Rewritten Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* Post-allocation scheduling of Tensix SFPU instructions.

   This pass runs immediately before pass_postreload, after register
   allocation and the Tensix opcode-synthesis and LREG-rename passes, and
   before replay formation, the Dst auto-increment fold and MOP formation.
   Those later passes recognize textual shapes in the scheduled stream, so
   every transformation here keeps unrolled row copies isomorphic, keeps
   counted replay-capture shapes intact, and never reorders across an
   explicit replay-buffer owner.  Formed SFPLOADMACRO calendars are opaque
   barriers.  Register references come from DF, which is complete for
   allocatable hard registers after allocation.  Every transformation is a
   reorder, a register rename, or a NOP insertion required by a delay
   contract; per-iteration semantics never change.

   In execute () order (each optimization has its own
   -mtt-tensix-optimize-* flag; only the NOP inserter always runs):

   1. Cross-row pairing: fuse two iterations of a counted single-row Dst
      loop into one doubled row and interleave the halves.
   2. Region list scheduling over straight-line regions of pure-LREG
      instructions, with cyclic (self-loop) and iterative modulo
      scheduling (Rau) variants judged on the steady-state initiation
      interval.
   3. Latency-bubble and NOP-shadow filling: move an independent
      instruction into a slot that would otherwise need an SFPNOP.
   4. Interlock-shadow filling: the same for transparent scoreboard
      stalls that cost a cycle but need no NOP.
   5. Capture rotation: close stalls across the seam of a replayed row.
   6. transform (): the NOP inserter for STATIC and DYNAMIC delay
      contracts.

   Each optimization commits only on a strict decrease of its modeled
   cost and restores the original insn chain exactly otherwise; refusals
   are reported by name through rvtt-refuse.h.  Timing facts (latency,
   stalls, makespan, cyclic II) come from rvtt-timing.h.  The end of the
   file provides drain-NOP elision services for SFPLOADMACRO formation.  */

#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "tree-pass.h"
#include "df.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "cfgrtl.h"
#include "print-rtl.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "insn-constants.h"
#include "recog.h"
#include "rvtt.h"
#include "tm-preds.h"
#include "rvtt-effects.h"
#include "rvtt-macro-tables.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-macro-region.h"
#include "rvtt-macro-sched.h"
#include "rvtt-macro-desc.h"
#include "rvtt-refuse.h"
#include "rvtt-timing.h"

namespace {

struct insn_regs
{
  HARD_REG_SET uses;
  HARD_REG_SET defs;
};

/* Collect INSN's SFPU hard-register uses and defs into REGS from DF.
   Return false if any reference is a pseudo or a non-SFPU register, or
   if INSN defines no SFPU register.  */

static bool
collect_sfpu_regs (rtx_insn *insn, insn_regs *regs)
{
  CLEAR_HARD_REG_SET (regs->uses);
  CLEAR_HARD_REG_SET (regs->defs);

  for (df_ref ref = DF_INSN_USES (insn); ref;
       ref = DF_REF_NEXT_LOC (ref))
    {
      unsigned regno = DF_REF_REGNO (ref);
      if (regno >= FIRST_PSEUDO_REGISTER || !SFPU_REG_P (regno))
	return false;
      SET_HARD_REG_BIT (regs->uses, regno);
    }
  for (df_ref ref = DF_INSN_DEFS (insn); ref;
       ref = DF_REF_NEXT_LOC (ref))
    {
      unsigned regno = DF_REF_REGNO (ref);
      if (regno >= FIRST_PSEUDO_REGISTER || !SFPU_REG_P (regno))
	return false;
      SET_HARD_REG_BIT (regs->defs, regno);
    }

  return !hard_reg_set_empty_p (regs->defs);
}

/* Return true if INSN may take part in the simple latency-bubble move:
   a recognized Tensix instruction marked reorder-safe
   (xtt_latency_reorder), with no memory reference and only SFPU register
   references, which are collected into REGS.  */

static bool
latency_reorderable_p (rtx_insn *insn, insn_regs *regs)
{
  return NONDEBUG_INSN_P (insn)
    && recog_memoized (insn) >= 0
    && get_attr_type (insn) == TYPE_TENSIX
    && get_attr_xtt_latency_reorder (insn) == XTT_LATENCY_REORDER_SAFE
    && !contains_mem_rtx_p (PATTERN (insn))
    && collect_sfpu_regs (insn, regs);
}

/* Return the next issued instruction after INSN in BB: the first
   following non-debug insn, if it is a recognized Tensix instruction with
   a nonzero length.  Return null at the block end or when that insn is
   anything else; callers treat that as an opaque boundary.  */

static rtx_insn *
next_issued_insn (basic_block bb, rtx_insn *insn)
{
  for (insn = NEXT_INSN (insn); insn != NEXT_INSN (BB_END (bb));
       insn = NEXT_INSN (insn))
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (recog_memoized (insn) < 0 || get_attr_type (insn) != TYPE_TENSIX
	  || !get_attr_length (insn))
	return nullptr;
      return insn;
    }
  return nullptr;
}

/* Return true if A and B share a register.  */

static bool
intersect_p (const HARD_REG_SET &a, const HARD_REG_SET &b)
{
  return hard_reg_set_intersect_p (a, b);
}

/* Move one independent ready instruction into a single exposed
   result-latency slot.  The NOP inserter runs afterwards and still
   handles target scoreboarding and WH/BH/QSR errata.  */
static void
fill_latency_bubbles (function *fn)
{
  df_analyze ();

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (rtx_insn *producer = BB_HEAD (bb); producer;)
      {
	rtx_insn *consumer = next_issued_insn (bb, producer);
	rtx_insn *filler = consumer ? next_issued_insn (bb, consumer) : nullptr;
	insn_regs producer_regs, consumer_regs, filler_regs;
	bool moved = (consumer && filler
		      && latency_reorderable_p (producer, &producer_regs)
		      && latency_reorderable_p (consumer, &consumer_regs)
		      && latency_reorderable_p (filler, &filler_regs)
		      && get_attr_xtt_delay_bubbles (producer) == 1
		      && intersect_p (producer_regs.defs, consumer_regs.uses)
		      && !intersect_p (producer_regs.defs, filler_regs.uses)
		      && !intersect_p (producer_regs.defs, filler_regs.defs)
		      && !intersect_p (consumer_regs.defs, filler_regs.uses)
		      && !intersect_p (consumer_regs.uses, filler_regs.defs)
		      && !intersect_p (consumer_regs.defs, filler_regs.defs));

	if (moved)
	  {
	    int filler_uid = INSN_UID (filler);
	    int producer_uid = INSN_UID (producer);
	    reorder_insns (filler, filler, producer);
	    if (dump_file)
	      fprintf (dump_file,
		       "Latency-fill moved uid=%d after producer uid=%d "
		       "target=%s\n", filler_uid, producer_uid,
		       TARGET_XTT_TENSIX_WH ? "wh" :
		       TARGET_XTT_TENSIX_BH ? "bh" : "qsr");
	    producer = consumer;
	  }
	else
	  producer = NEXT_INSN (producer);

	if (!producer || producer == NEXT_INSN (BB_END (bb)))
	  break;
      }
}

} /* anonymous namespace */

/* The target cost hook returns one for the STATIC/DYNAMIC delay
   contracts; it does not model instruction distance.  */
static unsigned
rvtt_delay_bubbles (rtx_insn *insn)
{
  unsigned nops = get_attr_xtt_delay_bubbles (insn);
  gcc_assert (nops <= 1);
  return nops;
}

/* Walk the BB graph from PROBE_INSN until we meet a TENSIX insn. Return true
   if REGNO != 0 and the TENSIX insn is dependent.  Return true if REGNO == 0
   and the TENSIX insn is not a NOP. Return false in all other cases. If we
   meet the end of a block, recurse into successor blocks and return the first
   true we get.  Populate VISITED with the BB's we marked. Takes advantage of
   no multi-register values, no return values and no clobbers of TENSIX
   registers. */

static bool
find_next_insn (std::vector<basic_block> &visited, basic_block bb, int regno,
		rtx_insn *probe_insn, bool first = true)
{
  if (bb->flags & BB_VISITED)
    return false;

  if (first)
    {
      if (probe_insn == BB_END (bb))
	goto walk_succs;
      probe_insn = NEXT_INSN (probe_insn);
    }
  else
    {
      /* Each block, other than the starting block, should only be
         walked once -- don't get trapped in a loop of non-TENSIX
         insns. The starting block should be walked exactly twice, if
         reachable from itself.  */
      bb->flags |= BB_VISITED;
      visited.push_back (bb);
      if (!probe_insn)
	goto walk_succs;
    }

  for (; probe_insn != NEXT_INSN (BB_END (bb));
       probe_insn = NEXT_INSN (probe_insn))
    {
      if (GET_CODE (probe_insn) != INSN)
	continue;
      rtx pattern = PATTERN (probe_insn);

      if (GET_CODE (pattern) == USE)
	// The case where this would be a dependency does not arise.
	continue;
      if (GET_CODE (pattern) == CLOBBER)
	continue;

      if (get_attr_type (probe_insn) != TYPE_TENSIX)
	continue;

      if (!regno)
	{
	  bool is_nop = recog_memoized (probe_insn) == CODE_FOR_rvtt_sfpnop;
	  if (dump_file)
	    {
	      fprintf (dump_file, "Found %snop insn ", is_nop ? "" : "non-");
	      dump_insn_slim (dump_file, probe_insn);
	    }
	  return !is_nop;
	}

      auto reg_used_p = [] (auto self, unsigned regno, rtx rtl) -> bool
      {
	switch (GET_CODE (rtl))
	  {
	  default:
	    // Unknown tensix insn component
	    gcc_unreachable ();
	    
	  case PARALLEL:
	  case UNSPEC:
	  case UNSPEC_VOLATILE:
	    {
	      // All 3 have the vector at position 0
	      auto &vec = XVEC (rtl, 0);
	      for (unsigned ix = GET_NUM_ELEM (vec); ix--;)
		if (self (self, regno, RTVEC_ELT (vec, ix)))
		  return true;
	    }
	    break;

	  case SET:
	    if (self (self, regno, SET_SRC (rtl)))
	      return true;
	    break;

	  case REG:
	    if (regno == REGNO (rtl))
	      return true;
	    break;

	  case CONST_INT:
	  case MEM:
	  case CLOBBER:
	  case USE:
	    break;
	  }
	return false;
      };

      bool is_dependent = reg_used_p (reg_used_p, regno, pattern);

      if (is_dependent)
	if (unsigned mask =
	    TARGET_XTT_TENSIX_BH ? XTT_DYNAMIC_BUG_BH :
	    TARGET_XTT_TENSIX_QSR ? XTT_DYNAMIC_BUG_QSR :
	    0)
	  // BH & QSR has scoreboarding, but with bugs
	  if (!(mask & get_attr_xtt_dynamic_bug (probe_insn)))
	    is_dependent = false;

      if (!is_dependent && !get_attr_length (probe_insn))
	continue;

      if (dump_file)
	{
	  fprintf (dump_file, "Found %sdependent insn ", is_dependent ? "" : "non-");
	  dump_insn_slim (dump_file, probe_insn);
	}
      return is_dependent;
    }

 walk_succs:;
  // Walk all the successors
  edge_iterator ei;
  edge e;
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (find_next_insn (visited, e->dest, regno, BB_HEAD (e->dest), false))
      return true;

  return false;
}

/* INSN is an sfpload insn, this must either be separated by 1 insn from any sfpstore
   that is the same location, or turned into an sfpmov of the stored value.

   1) We presume any store that is not obviously the same location, is a
   different location.

   2) If the store is in the same bb, we replace the load with an sfpassign.

   3) If it is in the only predecessor block we do the same.

   4) Otherwise we insert an sfpnop after the store.

   5) Don't forget to adjust any reg-dead note on the store's value.

*/

static void
handle_qsr_load_latency (std::vector<basic_block> &visited, basic_block bb, rtx_insn *insn,
			 rtx addr, bool first = true)
{
  if (bb->flags & BB_VISITED)
    return;

  rtx_insn *probe;
  if (first)
    {
      if (insn == BB_HEAD (bb))
	goto walk_preds;
      probe = PREV_INSN (insn);
    }
  else
    {
      bb->flags |= BB_VISITED;
      visited.push_back (bb);
      probe = BB_END (bb);
      if (!probe)
	goto walk_preds;
    }

  for (;;)
    {
      if (GET_CODE (probe) == INSN
	  && recog_memoized (probe) >= 0
	  && get_attr_type (probe) == TYPE_TENSIX
	  && get_attr_length (probe))
	{
	  if (INSN_CODE (probe) != CODE_FOR_rvtt_sfpstore_int)
	    // Met not-a-store, there is no hazard
	    return;

	  // There is a hazard if
	  // 1) the store's ADDR_MODE is 7 (non-incrementing)
	  // 2) and the addr is the same constant
	  // We ignore the problem of checking dynamic addresses, that's hard
	  // in non-ssa form.
	  auto store = XVECEXP (PATTERN (probe), 0, 0);
	  if (const0_rtx != XVECEXP (store, 0, rvtt_synth::IX_mem))
	    return;
	  if (INTVAL (XVECEXP (store, 0, rvtt_synth::IX_insn))
	      != INTVAL (addr))
	    // Addresses are different -- do we need to worry about halves?
	    return;
	  if (INTVAL (XVECEXP (store, 0, rvtt_synth::IX_src + 2)) != SFPLOADSTORE_ADDR_MODE_NOINC)
	    // Store addr_mod is side-effecting
	    return;

	  // We have a hit, insert a nop after the store It's likely that it's
	  // outside the loop, but the load could be inside one.
	  auto inserted = emit_insn_after (gen_rvtt_sfpnop (), probe);

	  if (dump_file)
	    {
	      fprintf (dump_file, "Resolving sfpstore...sfpload hazard by\n"
		       "emitting ");
	      dump_insn_slim (dump_file, inserted);
	      fprintf (dump_file, "after ");
	      dump_insn_slim (dump_file, probe);

	      fprintf (dump_file, "bacause of ");
	      dump_insn_slim (dump_file, insn);
	      fprintf (dump_file, "\n");
	    }

	  return;
	}

      if (probe == BB_HEAD (bb))
	break;
      probe = PREV_INSN (probe);
    }

 walk_preds:;
  // Walk all the predecessors
  edge_iterator ei;
  edge e;
  FOR_EACH_EDGE (e, ei, bb->preds)
    handle_qsr_load_latency (visited, e->src, insn, addr, false);
}

// Return true if the NOP inserter would pad INSN's delay DELAY (which
// must not be NONE).  This is the probe transform uses, shared so the
// shadow-filling phases can target and re-check exactly those sites.

static bool
delay_nop_needed_p (std::vector<basic_block> &visited, basic_block bb,
		    rtx_insn *insn, enum xtt_delay delay)
{
  bool insert = false;
  if (delay == XTT_DELAY_STATIC)
    {
      insert = find_next_insn (visited, bb, 0, insn);
      for (auto *bb : visited)
	bb->flags &= ~BB_VISITED;
      visited.clear ();
    }
  else
    {
      gcc_assert (delay == XTT_DELAY_DYNAMIC);
      auto find_next = [] (auto self, std::vector<basic_block> &visited,
			   basic_block bb, rtx_insn *insn, rtx rtl) -> bool
      {
	switch (GET_CODE (rtl))
	  {
	  default:
	    gcc_unreachable ();

	  case SET:
	    if (REG_P (SET_DEST (rtl)))
	      {
		unsigned regno = REGNO (SET_DEST (rtl));
		if (SFPU_REG_P (regno))
		  {
		    /* Writing to a constant reg falls on the floor */
		    bool insert = regno < SFPU_REG_FIRST + SFPU_CREG_IDX_LWM
		      && find_next_insn (visited, bb, regno, insn);

		    for (auto *bb : visited)
		      bb->flags &= ~BB_VISITED;
		    visited.clear ();

		    return insert;
		  }
	      }
	    break;

	  case PARALLEL:
	    {
	      auto &vec = XVEC (rtl, 0);
	      for (unsigned ix = GET_NUM_ELEM (vec); ix--;)
		if (self (self, visited, bb, insn, RTVEC_ELT (vec, ix)))
		  return true;
	    }
	    break;

	  case CLOBBER:
	  case SCRATCH:
	    break;
	  }

	return false;
      };

      insert = find_next (find_next, visited, bb, insn, PATTERN (insn));
    }
  return insert;
}

/* Latency-shadow filling.

   fill_latency_bubbles only moves the instruction immediately behind an
   exposed result-latency slot.  This phase targets exactly the bubbles
   the NOP inserter would pad (delay_nop_needed_p decides before and after)
   and fills each with the first independent instruction further down the
   block.

   - The filler must be a pure-LREG operation (only SFPU register
     references, no memory) and either the unpredicated LREG-to-LREG copy,
     marked XTT_LATENCY_REORDER_SAFE, or recorded in the effect table
     (rvtt_insn_effects) with no CC write, configuration access, RWC step
     or Dst traffic.
   - The bare copy reads no hidden state (CC, Dst, RWC, configuration), so
     it may cross any recognized non-replay-owner Tensix instruction with
     disjoint DF register sets: after allocation every effect on an
     allocatable LREG is visible in the pattern.  A CC-reading
     (lane-predicated) filler may only cross instructions that cannot
     write CC.
   - A predicated filler's writes also count as reads, since disabled
     lanes keep the destination's old contents.
   - A block containing an explicit replay-buffer owner is skipped: a
     fixed capture records the following words by position.
   - Unrecognized, opaque, zero-length or non-Tensix instructions end the
     search.
   - The move is kept only if the producer's bubble is closed, the filler
     opens none of its own, and no new bubble appears at the vacated
     position.

   STATIC delays pad before any non-NOP instruction, so no filler can
   close them.  */

/* Return true if INSN is the unpredicated LREG-to-LREG copy (the
   all-lanes SFPMOV form of the register-move pattern).  It writes every
   lane of its destination and reads only its source: no CC, Dst, RWC or
   configuration access.  */

static bool
bare_lreg_copy_p (rtx_insn *insn)
{
  rtx set = single_set (insn);
  return set && REG_P (SET_DEST (set)) && SFPU_REG_P (REGNO (SET_DEST (set)))
    && REG_P (SET_SRC (set)) && SFPU_REG_P (REGNO (SET_SRC (set)));
}

/* Return true if INSN is a recognized Tensix instruction that issues
   at least one instruction word (not a USE/CLOBBER marker or a
   zero-length pattern).  */

bool
issued_tensix_p (rtx_insn *insn)
{
  return GET_CODE (insn) == INSN
    && GET_CODE (PATTERN (insn)) != USE
    && GET_CODE (PATTERN (insn)) != CLOBBER
    && recog_memoized (insn) >= 0
    && get_attr_type (insn) == TYPE_TENSIX
    && get_attr_length (insn) > 0;
}

/* Return the SFPU-register references of INSN from DF; other
   references cannot conflict with a pure-LREG filler.  */

static void
sfpu_reg_refs (rtx_insn *insn, insn_regs *regs)
{
  CLEAR_HARD_REG_SET (regs->uses);
  CLEAR_HARD_REG_SET (regs->defs);
  for (df_ref ref = DF_INSN_USES (insn); ref; ref = DF_REF_NEXT_LOC (ref))
    if (DF_REF_REGNO (ref) < FIRST_PSEUDO_REGISTER
	&& SFPU_REG_P (DF_REF_REGNO (ref)))
      SET_HARD_REG_BIT (regs->uses, DF_REF_REGNO (ref));
  for (df_ref ref = DF_INSN_DEFS (insn); ref; ref = DF_REF_NEXT_LOC (ref))
    if (DF_REF_REGNO (ref) < FIRST_PSEUDO_REGISTER
	&& SFPU_REG_P (DF_REF_REGNO (ref)))
      SET_HARD_REG_BIT (regs->defs, DF_REF_REGNO (ref));
}

/* Return true if a moving filler may cross INSN.  A replay-buffer owner
   is never crossable.  If HIDDEN_FREE_FILLER (the bare all-lanes copy),
   any other recognized Tensix instruction may be crossed on register
   facts alone; otherwise INSN must not write CC.  */

static bool
shadow_crossing_safe_p (rtx_insn *insn, bool hidden_free_filler)
{
  if (get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
    return false;
  if (hidden_free_filler)
    /* The caller has checked issued_tensix_p; the hidden effects of a
       recognized Tensix pattern only touch state this filler ignores.  */
    return true;
  if (get_attr_xtt_latency_reorder (insn) == XTT_LATENCY_REORDER_SAFE)
    return true;
  if (bare_lreg_copy_p (insn))
    return true;
  xtt_effect_set e = rvtt_insn_effects (insn);
  return !e.opaque && !e.cc_write;
}

/* Return nonzero if INSN is an admissible filler; set *HIDDEN_FREE if
   it is the bare unpredicated copy.  */

static bool
shadow_filler_p (rtx_insn *insn, insn_regs *regs, bool *hidden_free)
{
  if (JUMP_P (insn) || !issued_tensix_p (insn)
      || contains_mem_rtx_p (PATTERN (insn))
      || !collect_sfpu_regs (insn, regs))
    return false;
  if (bare_lreg_copy_p (insn))
    {
      *hidden_free = true;
      return true;
    }
  *hidden_free = false;
  /* A CC-predicated write also reads its destination.  */
  regs->uses |= regs->defs;
  if (get_attr_xtt_latency_reorder (insn) == XTT_LATENCY_REORDER_SAFE)
    return true;
  xtt_effect_set e = rvtt_insn_effects (insn);
  return !e.opaque && !e.cc_write
    && !e.config_dests_written && !e.config_dests_read
    && e.rwc.kind == xtt_rwc_effect_t::NONE
    && !e.dst_mem_read && !e.dst_mem_write;
}

/* Maximum number of candidates examined by the two fill phases.  */
constexpr unsigned SEARCH_WINDOW = 24;

/* For every dynamic-delay producer in FN whose bubble the NOP inserter
   would pad, search up to SEARCH_WINDOW instructions further down the
   block for an independent filler and move it into the bubble (see the
   comment above).  */

static void
fill_nop_shadows (function *fn)
{
  std::vector<basic_block> visited;
  std::vector<rtx_insn *> crossed_insns;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *producer;
      FOR_BB_INSNS (bb, producer)
	{
	  if (GET_CODE (producer) != INSN || recog_memoized (producer) < 0
	      || get_attr_type (producer) != TYPE_TENSIX)
	    continue;
	  /* An explicit replay owner ends the eligible part of the
	     block.  */
	  if (get_attr_xtt_replay (producer) == XTT_REPLAY_OWNER)
	    break;
	  /* Only a dynamic delay can be closed by an independent filler.  */
	  if (get_attr_xtt_delay (producer) != XTT_DELAY_DYNAMIC)
	    continue;
	  visited.reserve (n_basic_blocks_for_fn (fn));
	  if (!delay_nop_needed_p (visited, bb, producer, XTT_DELAY_DYNAMIC))
	    continue;

	  insn_regs producer_regs;
	  sfpu_reg_refs (producer, &producer_regs);

	  rtx_insn *consumer = next_issued_insn (bb, producer);
	  if (!consumer)
	    continue;

	  insn_regs crossed;
	  CLEAR_HARD_REG_SET (crossed.uses);
	  CLEAR_HARD_REG_SET (crossed.defs);
	  crossed_insns.clear ();
	  unsigned scanned = 0;
	  rtx_insn *prev_issued = nullptr;
	  bool moved = false;
	  for (rtx_insn *cand = consumer;
	       cand && cand != NEXT_INSN (BB_END (bb))
	       && scanned != SEARCH_WINDOW && !moved;
	       cand = NEXT_INSN (cand))
	    {
	      if (!NONDEBUG_INSN_P (cand))
		continue;
	      /* USE/CLOBBER markers and zero-length patterns issue no
		 word; their register references still join the crossed
		 sets.  */
	      if (GET_CODE (cand) == INSN
		  && (GET_CODE (PATTERN (cand)) == USE
		      || GET_CODE (PATTERN (cand)) == CLOBBER
		      || (recog_memoized (cand) >= 0
			  && get_attr_type (cand) == TYPE_TENSIX
			  && !get_attr_length (cand))))
		{
		  insn_regs ghost_regs;
		  sfpu_reg_refs (cand, &ghost_regs);
		  crossed.uses |= ghost_regs.uses;
		  crossed.defs |= ghost_regs.defs;
		  continue;
		}
	      if (!issued_tensix_p (cand)
		  || get_attr_xtt_replay (cand) == XTT_REPLAY_OWNER)
		break;
	      ++scanned;

	      insn_regs cand_regs;
	      bool hidden_free;
	      if (cand != consumer
		  && shadow_filler_p (cand, &cand_regs, &hidden_free)
		  && !hard_reg_set_intersect_p (cand_regs.uses,
						producer_regs.defs)
		  && !hard_reg_set_intersect_p (cand_regs.defs,
						producer_regs.defs)
		  && !hard_reg_set_intersect_p (cand_regs.uses, crossed.defs)
		  && !hard_reg_set_intersect_p (cand_regs.defs, crossed.uses)
		  && !hard_reg_set_intersect_p (cand_regs.defs, crossed.defs))
		{
		  /* What may be crossed depends on the filler's class, so
		     check the whole crossed range per candidate.  */
		  bool crossable = true;
		  for (rtx_insn *x : crossed_insns)
		    if (!shadow_crossing_safe_p (x, hidden_free))
		      {
			crossable = false;
			break;
		      }

		  if (crossable)
		    {
		      /* Data for the vacated-position check.  */
		      bool prev_dynamic
			= (prev_issued
			   && get_attr_xtt_delay (prev_issued)
			      == XTT_DELAY_DYNAMIC);
		      bool prev_needed_before
			= prev_dynamic
			  && delay_nop_needed_p (visited, bb, prev_issued,
						 XTT_DELAY_DYNAMIC);
		      bool cand_dynamic
			= get_attr_xtt_delay (cand) == XTT_DELAY_DYNAMIC;
		      rtx_insn *restore_after = PREV_INSN (cand);
		      int cand_uid = INSN_UID (cand);

		      reorder_insns (cand, cand, producer);

		      bool closed
			= (!delay_nop_needed_p (visited, bb, producer,
						XTT_DELAY_DYNAMIC)
			   && (!cand_dynamic
			       || !delay_nop_needed_p (visited, bb, cand,
						       XTT_DELAY_DYNAMIC))
			   && (!prev_dynamic || prev_needed_before
			       || !delay_nop_needed_p (visited, bb,
						       prev_issued,
						       XTT_DELAY_DYNAMIC)));
		      if (closed)
			{
			  if (dump_file)
			    fprintf (dump_file,
				     "Shadow-fill moved uid=%d into the "
				     "bubble after uid=%d target=%s\n",
				     cand_uid, INSN_UID (producer),
				     TARGET_XTT_TENSIX_WH ? "wh" :
				     TARGET_XTT_TENSIX_BH ? "bh" : "qsr");
			  moved = true;
			  continue;
			}
		      reorder_insns (cand, cand, restore_after);
		    }
		}

	      insn_regs cross_regs;
	      sfpu_reg_refs (cand, &cross_regs);
	      crossed.uses |= cross_regs.uses;
	      crossed.defs |= cross_regs.defs;
	      crossed_insns.push_back (cand);
	      prev_issued = cand;
	    }
	}
    }
}

/* Interlock-stall filling.

   On targets with result scoreboarding (BH), a dependent back-to-back
   pair needs no NOP: the hardware stalls for a cycle instead, which still
   costs an issue slot on every replay of a capture.  This phase fills
   those modeled stalls using the xtt_result_latency attribute:

   - a stall is modeled only between an issued producer whose result
     latency is one and an immediately following issued consumer that
     references one of its SFPU destinations (as a read or a
     lane-predicated write);
   - producers without a known latency, or with a latency above one, are
     refused, so the two-adjacency accounting below is exact (the
     xtt_result_latency attribute value is latency + 1);
   - sites where the DYNAMIC probe fires are left to fill_nop_shadows;
   - the filler and every instruction whose adjacency changes must have a
     known latency, and the modeled stall count must decrease;
   - filler and crossing rules are those of fill_nop_shadows;
   - SFPSWAP's next-slot hazard (only SFPNOP may follow) is not a result
     latency: SFPSWAP is never a fill target, and since every non-NOP
     successor costs the same cycle, reordering around it does not change
     the count;
   - QSR has no latency data and is refused.  */

/* Return the result latency of INSN in issue slots, or -1 if INSN has
   opaque effects or no xtt_result_latency entry.  */

int
audited_latency (rtx_insn *insn)
{
  if (!issued_tensix_p (insn))
    return -1;
  xtt_effect_set e = rvtt_insn_effects (insn);
  // An instruction with the next-slot acceptance stall
  // (xtt_next_slot_stall, the SFPSWAP family) is refused even when it has a
  // result latency for the reissue cost model, so it never becomes a fill
  // target; see rvtt_timing::audited_latency.
  return rvtt_timing::audited_latency (e.opaque, e.next_slot_stall,
				       e.result_latency);
}

/* Return the modeled interlock stall cycles between issued P and an
   immediately following issued C: 0 if independent (or either is
   missing), the producer's latency if dependent, and -1 if dependent on a
   producer of unknown latency.  */

static int
adjacency_stall (rtx_insn *p, rtx_insn *c)
{
  if (!p || !c)
    return 0;
  insn_regs p_regs, c_regs;
  sfpu_reg_refs (p, &p_regs);
  if (hard_reg_set_empty_p (p_regs.defs))
    return 0;
  sfpu_reg_refs (c, &c_regs);
  bool dependent
    = hard_reg_set_intersect_p (p_regs.defs, c_regs.uses)
      || hard_reg_set_intersect_p (p_regs.defs, c_regs.defs);
  return rvtt_timing::adjacent_stall (dependent, audited_latency (p));
}

/* For every issued producer in FN with a one-slot latency and an
   immediately following dependent consumer, search the window for an
   independent zero-latency filler and move it into the stall (see the
   comment above).  The move is kept only if the modeled stall count
   strictly decreases and no new DYNAMIC pad site appears.  */

static void
fill_interlock_shadows (function *fn)
{
  if (!TARGET_XTT_TENSIX_BH && !TARGET_XTT_TENSIX_WH)
    {
      if (dump_file)
	fprintf (dump_file,
		 "Interlock fill refused: no audited latency facts for "
		 "this target\n");
      return;
    }

  df_analyze ();

  std::vector<basic_block> visited;
  std::vector<rtx_insn *> crossed_insns;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *producer;
      FOR_BB_INSNS (bb, producer)
	{
	  if (GET_CODE (producer) != INSN || recog_memoized (producer) < 0
	      || get_attr_type (producer) != TYPE_TENSIX)
	    continue;
	  /* An explicit replay owner ends the eligible part of the block.  */
	  if (get_attr_xtt_replay (producer) == XTT_REPLAY_OWNER)
	    break;
	  if (!get_attr_length (producer))
	    continue;

	  insn_regs producer_regs;
	  sfpu_reg_refs (producer, &producer_regs);
	  if (hard_reg_set_empty_p (producer_regs.defs))
	    continue;

	  rtx_insn *consumer = next_issued_insn (bb, producer);
	  if (!consumer)
	    continue;
	  insn_regs consumer_regs;
	  sfpu_reg_refs (consumer, &consumer_regs);
	  if (!hard_reg_set_intersect_p (producer_regs.defs,
					 consumer_regs.uses)
	      && !hard_reg_set_intersect_p (producer_regs.defs,
					    consumer_regs.defs))
	    continue;

	  int lat = audited_latency (producer);
	  if (lat < 0)
	    {
	      if (dump_file)
		fprintf (dump_file,
			 "Interlock fill refused: unaudited result latency "
			 "for producer uid=%d\n", INSN_UID (producer));
	      continue;
	    }
	  if (lat == 0)
	    continue;
	  if (lat > 1)
	    {
	      if (dump_file)
		fprintf (dump_file,
			 "Interlock fill refused: latency %d beyond the "
			 "audited window for producer uid=%d\n",
			 lat, INSN_UID (producer));
	      continue;
	    }

	  /* Required-NOP sites are left to fill_nop_shadows.  */
	  enum xtt_delay delay = get_attr_xtt_delay (producer);
	  if (delay == XTT_DELAY_STATIC)
	    continue;
	  visited.reserve (n_basic_blocks_for_fn (fn));
	  if (delay == XTT_DELAY_DYNAMIC
	      && delay_nop_needed_p (visited, bb, producer,
				     XTT_DELAY_DYNAMIC))
	    continue;

	  insn_regs crossed;
	  CLEAR_HARD_REG_SET (crossed.uses);
	  CLEAR_HARD_REG_SET (crossed.defs);
	  crossed_insns.clear ();
	  unsigned scanned = 0;
	  rtx_insn *prev_issued = nullptr;
	  bool moved = false;
	  for (rtx_insn *cand = consumer;
	       cand && cand != NEXT_INSN (BB_END (bb))
	       && scanned != SEARCH_WINDOW && !moved;
	       cand = NEXT_INSN (cand))
	    {
	      if (!NONDEBUG_INSN_P (cand))
		continue;
	      if (GET_CODE (cand) == INSN
		  && (GET_CODE (PATTERN (cand)) == USE
		      || GET_CODE (PATTERN (cand)) == CLOBBER
		      || (recog_memoized (cand) >= 0
			  && get_attr_type (cand) == TYPE_TENSIX
			  && !get_attr_length (cand))))
		{
		  insn_regs ghost_regs;
		  sfpu_reg_refs (cand, &ghost_regs);
		  crossed.uses |= ghost_regs.uses;
		  crossed.defs |= ghost_regs.defs;
		  continue;
		}
	      if (!issued_tensix_p (cand)
		  || get_attr_xtt_replay (cand) == XTT_REPLAY_OWNER)
		break;
	      ++scanned;

	      insn_regs cand_regs;
	      bool hidden_free;
	      if (cand != consumer
		  && shadow_filler_p (cand, &cand_regs, &hidden_free)
		  /* The filler must have zero latency so that no new stall
		     opens against the consumer.  */
		  && audited_latency (cand) == 0
		  && !hard_reg_set_intersect_p (cand_regs.uses,
						producer_regs.defs)
		  && !hard_reg_set_intersect_p (cand_regs.defs,
						producer_regs.defs)
		  && !hard_reg_set_intersect_p (cand_regs.uses, crossed.defs)
		  && !hard_reg_set_intersect_p (cand_regs.defs, crossed.uses)
		  && !hard_reg_set_intersect_p (cand_regs.defs, crossed.defs))
		{
		  bool crossable = true;
		  for (rtx_insn *x : crossed_insns)
		    if (!shadow_crossing_safe_p (x, hidden_free))
		      {
			crossable = false;
			break;
		      }

		  if (crossable)
		    {
		      /* Recount the stalls over the adjacencies the move
			 changes.  The producer/filler and filler/consumer
			 pairs are independent, so only the vacated seam
			 can stall.  */
		      rtx_insn *after = next_issued_insn (bb, cand);
		      int s_prev_cand = adjacency_stall (prev_issued, cand);
		      int s_cand_after = adjacency_stall (cand, after);
		      int s_prev_after = adjacency_stall (prev_issued, after);
		      if (s_prev_cand < 0 || s_cand_after < 0
			  || s_prev_after < 0)
			{
			  if (dump_file)
			    fprintf (dump_file,
				     "Interlock fill refused: unaudited "
				     "latency at the vacated seam of "
				     "uid=%d\n", INSN_UID (cand));
			}
		      else if (1 + s_prev_cand + s_cand_after
			       <= s_prev_after)
			{
			  if (dump_file)
			    fprintf (dump_file,
				     "Interlock fill refused: no modeled "
				     "stall decrease moving uid=%d\n",
				     INSN_UID (cand));
			}
		      else
			{
			  /* The move must not create a new DYNAMIC-delay pad
			     site.  */
			  bool prev_dynamic
			    = (prev_issued
			       && get_attr_xtt_delay (prev_issued)
				  == XTT_DELAY_DYNAMIC);
			  bool prev_needed_before
			    = prev_dynamic
			      && delay_nop_needed_p (visited, bb, prev_issued,
						     XTT_DELAY_DYNAMIC);
			  bool cand_dynamic
			    = get_attr_xtt_delay (cand) == XTT_DELAY_DYNAMIC;
			  rtx_insn *restore_after = PREV_INSN (cand);
			  int cand_uid = INSN_UID (cand);

			  reorder_insns (cand, cand, producer);

			  bool clean
			    = (!delay_nop_needed_p (visited, bb, producer,
						    XTT_DELAY_DYNAMIC)
			       && (!cand_dynamic
				   || !delay_nop_needed_p (visited, bb, cand,
							   XTT_DELAY_DYNAMIC))
			       && (!prev_dynamic || prev_needed_before
				   || !delay_nop_needed_p (visited, bb,
							   prev_issued,
							   XTT_DELAY_DYNAMIC)));
			  if (clean)
			    {
			      if (dump_file)
				fprintf (dump_file,
					 "Interlock-fill moved uid=%d into "
					 "the stall after uid=%d target=%s\n",
					 cand_uid, INSN_UID (producer),
					 TARGET_XTT_TENSIX_WH ? "wh" : "bh");
			      moved = true;
			      continue;
			    }
			  reorder_insns (cand, cand, restore_after);
			}
		    }
		}

	      insn_regs cross_regs;
	      sfpu_reg_refs (cand, &cross_regs);
	      crossed.uses |= cross_regs.uses;
	      crossed.defs |= cross_regs.defs;
	      crossed_insns.push_back (cand);
	      prev_issued = cand;
	    }
	}
    }
}

/* List scheduling of straight-line SFPU regions.

   The fill phases above move one instruction into one slot.  They cannot
   interleave two independent dependence chains (for example the P and Q
   polynomials of a rational approximation) so that every latency shadow
   is filled.  This phase builds the dependence DAG of a maximal
   straight-line region and list-schedules it, keeping the new order only
   if the modeled makespan strictly decreases.

   Region membership (every barrier bounds a region and stays put):
   - a node is an issued pure-LREG Tensix instruction that is either the
     bare unpredicated copy or recorded in the effect table with no CC
     write, configuration access, RWC step or Dst traffic.  Unlike
     shadow_filler_p, the XTT_LATENCY_REORDER_SAFE attribute alone is not
     enough;
   - a node's result latency must be known and at most one, since the
     distance model below is exact only for zero/one-slot latencies;
     otherwise the node is a barrier ("latency-beyond-audited-window");
   - CC-writing instructions are barriers, so a region executes under one
     CC state.  A lane-predicated write is a read-modify-write, but its
     implicit read targets only its own destinations, and the WAW and WAR
     edges on that register already carry every ordering the read
     requires;
   - a STATIC-delay contract is a barrier: reordering cannot close it;
   - an explicit replay-buffer owner ends the eligible part of the block,
     and formed macro emissions are opaque barriers.

   Interaction with the later formation passes:
   - a self-loop block is skipped: its row executes back-to-back across
     the backedge, which the linear boundary model mispredicts (capture
     rotation handles that seam);
   - two regions with the same insn-code signature in one block are both
     skipped ("repeated-row shape deferred to replay capture formation"),
     because unrolled row copies must stay textually identical for the
     replay and MOP re-roll;
   - -mtt-tensix-optimize-round-interleave lifts both restrictions for
     the shapes described in the round-chain interleave section below.

   Dependence edges come from DF hard-register references.  RAW and WAW
   edges require an issue distance of at least words(producer) +
   latency(producer); WAR edges require at least words(producer).

   The objective is the modeled makespan: issue slots (multi-word
   instructions count each word) plus modeled interlock stalls (SFPNOP
   words on WH, scoreboard stalls on BH), plus boundary terms:
   - the preceding issued Tensix instruction (the entry producer) adds
     its latency to the nodes that touch its destinations.  With all
     latencies at most one, that instruction is the whole entry horizon;
   - if the entry producer's latency is unknown or above one, every node
     touching its destinations is pinned to its original issue slot or
     later ("entry-pinned"), so the unmodeled stall can only shrink.  The
     first node is always pinned to slot zero, so a pinned node can only
     be first if it already was, which also keeps the entry producer's
     DYNAMIC pad state unchanged;
   - the following issued Tensix instruction (the exit consumer) adds the
     trailing shadow of the nodes that feed it; a region ending the block
     drains every node's shadow.

   Ready nodes issue by greatest critical-path height, ties broken by
   original order.  Register pressure is not a concern after allocation:
   register reuse appears as WAR/WAW edges, so no order needs an extra
   register.

   A candidate order is kept only on a strict makespan decrease, and only
   if the number of DYNAMIC-delay pad sites over the region does not grow
   and the entry producer's pad state does not flip on; otherwise the
   original chain is restored exactly, debug insns included.  Debug insns
   keep their original chain links.  */

namespace {

struct ls_node
{
  rtx_insn *insn;
  insn_regs regs;	 /* uses include defs for predicated writes  */
  HARD_REG_SET raw_defs; /* defs alone				      */
  int lat;		 /* result latency			      */
  int words;		 /* issue slots this instruction occupies    */
  int orig;		 /* original index within the region	      */
  long cp;		 /* critical-path height to region exit      */
  int ready;		 /* earliest issue slot (filled during sim)  */
  int entry_pin;	 /* issue slot floor from the entry boundary */
  bool pin_to_baseline;	 /* unaudited entry producer dependence      */
};

} /* anonymous namespace */

/* Return true if INSN may be a region node; otherwise set *WHY to the
   barrier class.  */

static bool
ls_admissible_p (rtx_insn *insn, ls_node *node, const char **why)
{
  if (!issued_tensix_p (insn))
    {
      *why = "non-tensix";
      return false;
    }
  if (JUMP_P (insn))
    {
      *why = "control-flow";
      return false;
    }
  if (get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
    {
      *why = "replay-owner";
      return false;
    }
  if (contains_mem_rtx_p (PATTERN (insn)))
    {
      *why = "memory-operand";
      return false;
    }
  if (!bare_lreg_copy_p (insn))
    {
      xtt_effect_set e = rvtt_insn_effects (insn);
      if (e.opaque)
	{
	  *why = "effect-opaque";
	  return false;
	}
      if (e.cc_write)
	{
	  *why = "cc-write";
	  return false;
	}
      if (e.config_dests_written || e.config_dests_read)
	{
	  *why = "config-access";
	  return false;
	}
      if (e.rwc.kind != xtt_rwc_effect_t::NONE)
	{
	  *why = "rwc-step";
	  return false;
	}
      if (e.dst_mem_read || e.dst_mem_write)
	{
	  *why = "dst-access";
	  return false;
	}
      /* A lane-predicated write's implicit read is ordered by the
	 WAW and WAR edges on its destination (see the comment above),
	 so defs need not be added to uses.  */
    }
  node->lat = audited_latency (insn);
  if (node->lat < 0)
    {
      *why = "unaudited-latency";
      return false;
    }
  if (node->lat > 1)
    {
      /* The distance model is exact only for zero/one-slot
	 latencies.  */
      *why = "latency-beyond-audited-window";
      return false;
    }
  if (get_attr_xtt_delay (insn) == XTT_DELAY_STATIC)
    {
      *why = "static-delay";
      return false;
    }
  if (!collect_sfpu_regs (insn, &node->regs))
    {
      *why = "scalar-or-defless";
      return false;
    }
  node->raw_defs = node->regs.defs;
  node->insn = insn;
  node->words = get_attr_length (insn) / 4;
  if (node->words < 1)
    {
      *why = "zero-length";
      return false;
    }
  return true;
}

/* Return how the earlier node P orders the later node C: 0 not at all,
   1 latency-weighted (RAW or WAW), 2 issue order only (WAR).  */

static int
ls_dependence (const ls_node &p, const ls_node &c)
{
  return rvtt_timing::classify_dependence
    (hard_reg_set_intersect_p (p.raw_defs, c.regs.uses)
     || hard_reg_set_intersect_p (p.raw_defs, c.raw_defs),
     hard_reg_set_intersect_p (p.regs.uses, c.raw_defs));
}

/* Convert the region NODES into the timing engine's representation:
   per-node {words, lat, entry_pin} plus the full dependence matrix (both
   directions; the diagonal carries the cross-iteration self-dependence
   used by the cyclic model).  */

static rvtt_timing::seq
ls_timing_seq (const std::vector<ls_node> &nodes)
{
  rvtt_timing::seq s;
  unsigned n = nodes.size ();
  s.ops.resize (n);
  s.dep.resize (n * n);
  for (unsigned i = 0; i != n; ++i)
    {
      s.ops[i].words = nodes[i].words;
      s.ops[i].lat = nodes[i].lat;
      s.ops[i].entry_pin = nodes[i].entry_pin;
      for (unsigned j = 0; j != n; ++j)
	s.dep[i * n + j]
	  = (unsigned char) ls_dependence (nodes[i], nodes[j]);
    }
  return s;
}

/* Return the modeled issue timeline of NODES in the order ORDER (indices
   into NODES), filling *ISSUE (indexed like NODES) with issue slots.  The
   result is the last occupied slot boundary plus the trailing shadow of
   every node in EXIT_MASK (nodes feeding the exit consumer, or all nodes
   when the region ends the block).  Entry pins and the entry producer's
   latency floor are honored.  */

static int
ls_simulate (const std::vector<ls_node> &nodes,
	     const std::vector<int> &order, std::vector<int> *issue,
	     const std::vector<bool> &exit_shadow)
{
  return rvtt_timing::simulate (ls_timing_seq (nodes), order, issue,
				exit_shadow);
}

/* Count the DYNAMIC-delay pad sites over the region members, using the
   NOP inserter's probe.  */

static unsigned
ls_pad_sites (std::vector<basic_block> &visited, basic_block bb,
	      const std::vector<ls_node> &nodes)
{
  unsigned pads = 0;
  for (const ls_node &n : nodes)
    if (get_attr_xtt_delay (n.insn) == XTT_DELAY_DYNAMIC
	&& delay_nop_needed_p (visited, bb, n.insn, XTT_DELAY_DYNAMIC))
      ++pads;
  return pads;
}

/* Return a list schedule of NODES that honors the DAG: critical-path
   priority, original order on ties.  */

static std::vector<int>
ls_list_order (std::vector<ls_node> &nodes)
{
  unsigned n = nodes.size ();

  /* Critical-path heights over the issue-distance weights.  */
  for (unsigned i = n; i--;)
    {
      long cp = nodes[i].words + nodes[i].lat;
      for (unsigned j = i + 1; j != n; ++j)
	{
	  int kind = ls_dependence (nodes[i], nodes[j]);
	  if (!kind)
	    continue;
	  long via = nodes[j].cp
	    + nodes[i].words + (kind == 1 ? nodes[i].lat : 0);
	  if (via > cp)
	    cp = via;
	}
      nodes[i].cp = cp;
    }

  std::vector<int> order;
  std::vector<bool> issued (n, false);
  std::vector<int> ready_at (n, 0);
  for (unsigned i = 0; i != n; ++i)
    ready_at[i] = nodes[i].entry_pin;

  int t = 0;
  while (order.size () != n)
    {
      int best = -1;
      int soonest = INT_MAX;
      for (unsigned i = 0; i != n; ++i)
	{
	  if (issued[i])
	    continue;
	  /* Ready when every earlier dependence has issued and its
	     distance is satisfied.  */
	  int ready = ready_at[i];
	  bool deps_done = true;
	  for (unsigned j = 0; j != n; ++j)
	    {
	      if (j == i || nodes[j].orig > nodes[i].orig)
		continue;
	      int kind = ls_dependence (nodes[j], nodes[i]);
	      if (!kind)
		continue;
	      if (!issued[j])
		{
		  deps_done = false;
		  break;
		}
	    }
	  if (!deps_done)
	    continue;
	  if (ready < soonest)
	    soonest = ready;
	  if (ready > t)
	    continue;
	  if (best < 0 || nodes[i].cp > nodes[best].cp
	      || (nodes[i].cp == nodes[best].cp
		  && nodes[i].orig < nodes[best].orig))
	    best = i;
	}
      if (best < 0)
	{
	  /* Nothing ready: advance to the earliest ready time (a
	     modeled stall).  */
	  gcc_assert (soonest != INT_MAX && soonest > t);
	  t = soonest;
	  continue;
	}
      order.push_back (best);
      issued[best] = true;
      int done = t + nodes[best].words;
      /* Successor readiness floors.  */
      for (unsigned j = 0; j != n; ++j)
	{
	  if (issued[j] || j == (unsigned) best)
	    continue;
	  int kind = ls_dependence (nodes[best], nodes[j]);
	  if (!kind)
	    continue;
	  int need = done + (kind == 1 ? nodes[best].lat : 0);
	  if (need > ready_at[j])
	    ready_at[j] = need;
	}
      t = done;
    }
  return order;
}

/* Schedule one region.  NODES are the region members in original order
   (orig fields set).  ANCHOR is the unmoved insn immediately before the
   region.  UNAUDITED_DEFS holds the entry producer's defs when its
   latency is unknown or above one, and is empty otherwise.  If
   FORCED_ORDER is given it replaces the list heuristic (used to apply one
   region's permutation to an isomorphic sibling; the caller has checked
   that the dependence matrices are equal).  If CHOSEN_ORDER is given it
   receives the committed permutation.  Return true if the region was
   reordered.  */

static bool
ls_schedule_region (basic_block bb, std::vector<ls_node> &nodes,
		    rtx_insn *anchor, rtx_insn *entry_producer,
		    rtx_insn *exit_consumer,
		    const HARD_REG_SET &unaudited_defs,
		    std::vector<basic_block> &visited,
		    const std::vector<int> *forced_order = nullptr,
		    std::vector<int> *chosen_order = nullptr)
{
  unsigned n = nodes.size ();

  /* Entry boundary: latency floor from the entry producer.  A
     producer with unknown or long latency is handled through
     UNAUDITED_DEFS instead.  */
  insn_regs ep_regs;
  CLEAR_HARD_REG_SET (ep_regs.uses);
  CLEAR_HARD_REG_SET (ep_regs.defs);
  int ep_lat = 0;
  if (entry_producer)
    {
      sfpu_reg_refs (entry_producer, &ep_regs);
      ep_lat = audited_latency (entry_producer);
      if (ep_lat < 0 || ep_lat > 1)
	ep_lat = 0;
    }
  for (unsigned i = 0; i != n; ++i)
    {
      nodes[i].entry_pin = 0;
      nodes[i].pin_to_baseline
	= hard_reg_set_intersect_p (unaudited_defs, nodes[i].regs.uses)
	  || hard_reg_set_intersect_p (unaudited_defs, nodes[i].raw_defs);
      if (entry_producer
	  && (hard_reg_set_intersect_p (ep_regs.defs, nodes[i].regs.uses)
	      || hard_reg_set_intersect_p (ep_regs.defs, nodes[i].raw_defs))
	  && ep_lat > nodes[i].entry_pin)
	nodes[i].entry_pin = ep_lat;
    }

  /* Exit boundary: nodes feeding the next issued insn keep their
     trailing shadow; a region ending the block drains everything.  */
  std::vector<bool> exit_shadow (n, false);
  if (exit_consumer)
    {
      insn_regs xc;
      sfpu_reg_refs (exit_consumer, &xc);
      HARD_REG_SET wanted = xc.uses;
      wanted |= xc.defs;
      for (unsigned i = 0; i != n; ++i)
	exit_shadow[i]
	  = hard_reg_set_intersect_p (nodes[i].raw_defs, wanted);
    }
  else
    for (unsigned i = 0; i != n; ++i)
      exit_shadow[i] = true;

  /* Baseline: the original order under the same model.  */
  std::vector<int> base_order (n);
  for (unsigned i = 0; i != n; ++i)
    base_order[i] = i;
  std::vector<int> base_issue (n, 0);
  int base_end = ls_simulate (nodes, base_order, &base_issue, exit_shadow);

  /* Apply the pins only after the baseline itself is modeled.  */
  for (unsigned i = 0; i != n; ++i)
    if (nodes[i].pin_to_baseline)
      nodes[i].entry_pin = base_issue[i];

  /* Candidate.  */
  std::vector<int> order
    = forced_order ? *forced_order : ls_list_order (nodes);
  std::vector<int> cand_issue (n, 0);
  int cand_end = ls_simulate (nodes, order, &cand_issue, exit_shadow);

  if (cand_end >= base_end)
    {
      if (dump_file)
	fprintf (dump_file, "List-schedule refused: no modeled makespan "
		 "decrease in bb %d region at uid=%d (%d -> %d)\n",
		 bb->index, INSN_UID (nodes[0].insn), base_end, cand_end);
      return false;
    }

  /* Data for the commit check: the pad-site count over the region and
     the entry producer's pad state, which can change when a different
     member becomes first.  */
  unsigned pads_before = ls_pad_sites (visited, bb, nodes);
  bool ep_dynamic
    = entry_producer
      && get_attr_xtt_delay (entry_producer) == XTT_DELAY_DYNAMIC;
  bool ep_needed_before
    = ep_dynamic
      && delay_nop_needed_p (visited, bb, entry_producer, XTT_DELAY_DYNAMIC);

  /* Record the chain from ANCHOR to the region's last member, debug
     insns included, for exact restoration.  Notes are not recorded;
     they emit no code.  */
  std::vector<rtx_insn *> chain;
  for (rtx_insn *w = NEXT_INSN (anchor);; w = NEXT_INSN (w))
    {
      if (INSN_P (w))
	chain.push_back (w);
      if (w == nodes[n - 1].insn)
	break;
    }

  /* Commit: relink the region in schedule order after ANCHOR.  */
  rtx_insn *after = anchor;
  for (unsigned k = 0; k != n; ++k)
    {
      rtx_insn *insn = nodes[order[k]].insn;
      if (PREV_INSN (insn) != after)
	reorder_insns (insn, insn, after);
      after = insn;
    }

  unsigned pads_after = ls_pad_sites (visited, bb, nodes);
  bool ep_flipped
    = ep_dynamic && !ep_needed_before
      && delay_nop_needed_p (visited, bb, entry_producer,
			     XTT_DELAY_DYNAMIC);
  if (pads_after > pads_before || ep_flipped)
    {
      /* Restore the recorded chain exactly (debug insns included).  */
      after = anchor;
      for (rtx_insn *insn : chain)
	{
	  if (PREV_INSN (insn) != after)
	    reorder_insns (insn, insn, after);
	  after = insn;
	}
      if (dump_file)
	fprintf (dump_file, "List-schedule refused: %s, restored bb %d "
		 "region at uid=%d\n",
		 ep_flipped ? "entry-producer pad flip"
			    : "pad-site increase",
		 bb->index, INSN_UID (nodes[0].insn));
      return false;
    }

  if (dump_file)
    {
      fprintf (dump_file, "List-schedule: bb %d nodes=%u makespan "
	       "%d -> %d target=%s\n",
	       bb->index, n, base_end, cand_end,
	       TARGET_XTT_TENSIX_WH ? "wh" : "bh");
      for (unsigned k = 0; k != n; ++k)
	fprintf (dump_file, "List-schedule slot=%d uid=%d\n",
		 cand_issue[order[k]], INSN_UID (nodes[order[k]].insn));
    }
  if (chosen_order)
    *chosen_order = order;
  return true;
}

/* Round-chain interleave (-mtt-tensix-optimize-round-interleave).

   (1) Self-loop rows.  Instead of the linear boundary terms, a row is
       judged on the steady-state initiation interval of its wrapped
       dependence model (the body simulated as back-to-back copies until
       two successive iteration start distances agree), and the new
       order is kept only on a strict II decrease.  The reorder never
       crosses the backedge.  The row must be a single region with no
       other issued Tensix word, no replay owner and no call.
       Cross-block producers into the first iteration are not modeled,
       as for block-head regions in the straight-line case.  Before
       scheduling, ls_cyclic_rename_collisions breaks false WAW/WAR
       recurrences that the allocator introduced between the unrolled
       copies; a refusal restores the original registers.

   (2) Pairs of isomorphic regions.  For exactly two regions with the
       same insn-code signature and equal positional dependence
       matrices, the first region's list order is applied to both, so
       the copies stay isomorphic.  Each region is judged by its own
       boundary model and the pair commits or restores together.  Three
       or more copies are left to replay formation, and unequal
       matrices are refused ("copies-not-dataflow-isomorphic").  */

/* Queue every occurrence of hard reg OLDR inside *LOC as a
   validate_change to NEWR.  */

static void
ls_queue_reg_replacements (rtx_insn *insn, rtx *loc, unsigned oldr,
			   unsigned newr)
{
  rtx x = *loc;
  if (!x)
    return;
  if (REG_P (x))
    {
      if (REGNO (x) == oldr)
	validate_change (insn, loc, gen_rtx_REG (GET_MODE (x), newr), true);
      return;
    }
  const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (int i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; --i)
    {
      if (fmt[i] == 'e')
	ls_queue_reg_replacements (insn, &XEXP (x, i), oldr, newr);
      else if (fmt[i] == 'E')
	for (int j = XVECLEN (x, i) - 1; j >= 0; --j)
	  ls_queue_reg_replacements (insn, &XVECEXP (x, i, j), oldr, newr);
    }
}

/* Storage-collision rename for a cyclic row.  The allocator packs the
   short lifetimes of two unrolled copies into the same LREGs, creating a
   false WAW/WAR recurrence that serializes the row.  A colliding
   definition web (node I defining reg R that an earlier region node also
   defines) moves to an unused LREG F when:
   - R is not live into the block (which excludes every loop-carried
     value);
   - the value dies inside the region: a later region node writes R, or
     R is not live out of the block;
   - the defining write is not a read-modify-write of R;
   - F is an allocatable, non-fixed LREG that no region node touches and
     that is not live into or out of the block.
   The web is node I's definition plus every following reader of R,
   continuing through read-modify-write redefinitions of R (an in-place
   operation such as SFPMULI both consumes and continues the value), and
   ending before the next writer of R that does not read it.  Each web
   gets a fresh F, so the recorded renames can be undone exactly.

   This is correct under lane predication because a region executes under
   one CC state: every region write uses the same lane mask, and the
   region's outputs on disabled lanes come from pre-region register
   contents, which the rename does not touch.  */

struct ls_rename
{
  unsigned oldr, newr;
  std::vector<rtx_insn *> insns;	/* web members rewritten */
};

/* Refresh every node's cached register sets from its pattern, so that
   dependence tests see renamed registers.  */

static void
ls_refresh_node_regs (std::vector<ls_node> &nodes)
{
  for (ls_node &nd : nodes)
    {
      collect_sfpu_regs (nd.insn, &nd.regs);
      nd.raw_defs = nd.regs.defs;
    }
}

/* Rename the colliding definition webs of the region NODES in BB to
   unused LREGs, as described above.  If START_ALLOWED is given, only
   those nodes may root a web.  SCAN_ORDER changes only the order in which
   roots are offered free registers.  *NO_FREE_LREG, if given, is set once
   no free register is found.  Every rename is appended to *RECORD.
   Return true if at least one web was renamed.  */

static bool
ls_cyclic_rename_collisions (basic_block bb, std::vector<ls_node> &nodes,
			     std::vector<ls_rename> *record,
			     const std::vector<bool> *start_allowed = nullptr,
			     const std::vector<unsigned> *scan_order = nullptr,
			     bool *no_free_lreg = nullptr)
{
  unsigned n = nodes.size ();
  bool any = false;
  /* SCAN_ORDER only decides which roots are offered free registers
     first; webs, collisions and rewrites follow stream order.  Cross-row
     pairing passes the second copy first, since breaking its
     serialization is what the pairing needs.  */
  for (unsigned ii = 0; ii != n; ++ii)
    {
    unsigned i = scan_order ? (*scan_order)[ii] : ii;
    for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
      {
	if (!TEST_HARD_REG_BIT (nodes[i].raw_defs, r))
	  continue;
	if (start_allowed && !(*start_allowed)[i])
	  {
	    /* A definition inside a CC atom is lane-predicated, so
	       renaming its web to a dead LREG would leave stale bits in
	       the disabled lanes of the new register where the original
	       one held the pre-atom value.  Only webs rooted where all
	       lanes are enabled may be renamed.  */
	    rvtt_refuse (RVTT_REF_CROSSROW_PAIRING_RENAME_CC_DOMAIN, dump_file,
			 "List-schedule rename refused: "
			 "crossrow-pairing-rename-cc-domain reg %u uid=%d\n",
			 r, INSN_UID (nodes[i].insn));
	    continue;
	  }
	bool earlier = false;
	for (unsigned j = 0; j != i && !earlier; ++j)
	  earlier = TEST_HARD_REG_BIT (nodes[j].raw_defs, r);
	if (!earlier)
	  continue;
	if (TEST_HARD_REG_BIT (nodes[i].regs.uses, r))
	  {
	    /* A read-modify-write definition reads the colliding value;
	       a web must start with a fresh value.  */
	    rvtt_refuse (RVTT_REF_ROUND_INTERLEAVE_RENAME_RMW_DEF, dump_file,
			 "List-schedule rename refused: "
			 "round-interleave-rename-rmw-def reg %u uid=%d\n",
			 r, INSN_UID (nodes[i].insn));
	    continue;
	  }
	if (REGNO_REG_SET_P (df_get_live_in (bb), r))
	  {
	    rvtt_refuse (RVTT_REF_ROUND_INTERLEAVE_RENAME_LIVE_IN, dump_file,
			 "List-schedule rename refused: "
			 "round-interleave-rename-live-in reg %u uid=%d\n",
			 r, INSN_UID (nodes[i].insn));
	    continue;
	  }
	/* Web extent: from the definition at I, through every reader
	   and read-modify-write redefinition of R, ending before the
	   next writer of R that does not read it.  Reaching the region
	   end instead requires R to be dead out of the block.  */
	unsigned extent_end = n;	/* exclusive */
	bool fresh_terminator = false;
	for (unsigned k = i + 1; k != n; ++k)
	  if (TEST_HARD_REG_BIT (nodes[k].raw_defs, r)
	      && !TEST_HARD_REG_BIT (nodes[k].regs.uses, r))
	    {
	      extent_end = k;
	      fresh_terminator = true;
	      break;
	    }
	if (!fresh_terminator && REGNO_REG_SET_P (df_get_live_out (bb), r))
	  {
	    rvtt_refuse (RVTT_REF_ROUND_INTERLEAVE_RENAME_LIVE_OUT, dump_file,
			 "List-schedule rename refused: "
			 "round-interleave-rename-live-out reg %u uid=%d\n",
			 r, INSN_UID (nodes[i].insn));
	    continue;
	  }

	int f = -1;
	for (unsigned c = SFPU_REG_FIRST; c <= SFPU_REG_LAST && f < 0; ++c)
	  {
	    if (fixed_regs[c])
	      continue;
	    bool touched = false;
	    for (unsigned j = 0; j != n && !touched; ++j)
	      touched = TEST_HARD_REG_BIT (nodes[j].regs.uses, c)
			|| TEST_HARD_REG_BIT (nodes[j].raw_defs, c);
	    if (touched
		|| REGNO_REG_SET_P (df_get_live_in (bb), c)
		|| REGNO_REG_SET_P (df_get_live_out (bb), c))
	      continue;
	    f = (int) c;
	  }
	if (f < 0)
	  {
	    if (no_free_lreg)
	      *no_free_lreg = true;
	    rvtt_refuse (RVTT_REF_ROUND_INTERLEAVE_RENAME_NO_FREE_LREG,
			 dump_file,
			 "List-schedule rename refused: "
			 "round-interleave-rename-no-free-lreg reg %u "
			 "uid=%d\n", r, INSN_UID (nodes[i].insn));
	    return any;
	  }

	ls_rename rn;
	rn.oldr = r;
	rn.newr = (unsigned) f;
	ls_queue_reg_replacements (nodes[i].insn, &PATTERN (nodes[i].insn),
				   r, (unsigned) f);
	rn.insns.push_back (nodes[i].insn);
	for (unsigned k = i + 1; k != extent_end; ++k)
	  if (TEST_HARD_REG_BIT (nodes[k].regs.uses, r)
	      || TEST_HARD_REG_BIT (nodes[k].raw_defs, r))
	    {
	      ls_queue_reg_replacements (nodes[k].insn,
					 &PATTERN (nodes[k].insn),
					 r, (unsigned) f);
	      rn.insns.push_back (nodes[k].insn);
	    }
	if (!apply_change_group ())
	  {
	    rvtt_refuse (RVTT_REF_ROUND_INTERLEAVE_RENAME_CONSTRAINT, dump_file,
			 "List-schedule rename refused: "
			 "round-interleave-rename-constraint reg %u "
			 "uid=%d\n", r, INSN_UID (nodes[i].insn));
	    continue;
	  }
	for (rtx_insn *ins : rn.insns)
	  df_insn_rescan (ins);
	if (dump_file)
	  fprintf (dump_file, "List-schedule rename: reg %u -> %u web at "
		   "uid=%d (%zu insns) in bb %d\n",
		   r, (unsigned) f, INSN_UID (nodes[i].insn),
		   rn.insns.size (), bb->index);
	record->push_back (std::move (rn));
	ls_refresh_node_regs (nodes);
	any = true;
      }
    }
  return any;
}

/* Undo every recorded rename (each web's F was unused before, so
   replacing F with R over the web is unambiguous).  */

static void
ls_undo_renames (std::vector<ls_rename> &record)
{
  for (unsigned i = record.size (); i--;)
    {
      ls_rename &rn = record[i];
      for (rtx_insn *ins : rn.insns)
	ls_queue_reg_replacements (ins, &PATTERN (ins), rn.newr, rn.oldr);
      bool ok = apply_change_group ();
      gcc_assert (ok);
      for (rtx_insn *ins : rn.insns)
	df_insn_rescan (ins);
    }
  record.clear ();
}

/* Return the steady-state initiation interval of NODES issued
   repeatedly in ORDER, as for a self-loop row.  Entry pins do not apply;
   dependences across copies use ls_dependence.  */

static int
ls_cyclic_ii (const std::vector<ls_node> &nodes,
	      const std::vector<int> &order)
{
  return rvtt_timing::cyclic_ii (ls_timing_seq (nodes), order);
}

/* Iterative modulo scheduling (-mtt-tensix-optimize-ims).

   This adds a candidate order from Rau's iterative modulo scheduling to
   the cyclic paths (the single-region self-loop row and the interior
   regions of ls_schedule_cyclic_interior).  The engine is in
   rvtt-timing.h: MII = max (ResMII, RecMII) over the dependence-distance
   graph built by make_mod_prob from the same data the acceptance model
   uses, then budgeted-eviction placement in a single-issue modulo
   reservation table.

   The result is applied as a permutation within the region (placement
   sorted by issue slot, original index on ties) and accepted by the same
   rule as the list order: a strict steady-state II decrease under
   ls_cyclic_ii and the pad-site checks.  Barrier words and the backedge
   are never crossed.

   A placement whose value lifetimes exceed the II needs
   kmin = ceil (maxlife / II) kernel copies (modulo variable expansion,
   Lam, PLDI 1988).  This path only offers the flat order; the expansion
   itself is done by the cross-row pairing's mve-expand option.  The
   candidate is refused as mve-rename-exhausted when the live copies
   would not fit the register file net of loop-live invariants, or when
   the region rename already ran out of free LREGs.

   Refusals:
     ims-unaudited-latency             a row word with a result or
					next-slot stall but no known
					latency of at most one
     ims-budget-exhausted              the eviction budget ran out at
					every II below the bound
     ims-no-ii-decrease                MII at or above the current II,
					or no accepted candidate
     ims-order-hazard                  the legality re-check failed
					(not expected to fire)
     ims-dependence-distance-unproven  a cross-iteration edge of
					distance greater than one
     mve-rename-exhausted              kmin > 1 and the copies do not
					fit (see above)  */

/* Capacity of the allocatable vector-register file.  */
extern unsigned rvtt_pressure_capacity ();

namespace {

struct ls_ims_candidate
{
  std::vector<int> order;
  int resmii = 0;
  int recmii = 0;
  int mii = 0;
  int place_ii = 0;
  int kmin = 1;
  unsigned demand = 0;
};

} /* anonymous namespace */

/* Generate the IMS candidate order for the region NODES.  MAX_II is the
   acceptance bound: only a placement with a strictly smaller II is
   useful.  RENAME_EXHAUSTED says the region renamer found no free LREG.
   Return false with *WHY set on refusal.  NODES is not modified.  */

static bool
ls_ims_order (basic_block bb, const std::vector<ls_node> &nodes,
	      int max_ii, bool rename_exhausted, ls_ims_candidate *out)
{
  const unsigned n = nodes.size ();
  rvtt_timing::mod_prob prob
    = rvtt_timing::make_mod_prob (ls_timing_seq (nodes));

  /* make_mod_prob only produces distances 0 and 1; refuse anything
     else.  */
  for (unsigned k = 0; k != prob.edges.size (); ++k)
    if (prob.edges[k].omega > 1)
      {
	rvtt_refuse (RVTT_REF_IMS_DEPENDENCE_DISTANCE_UNPROVEN, dump_file,
		     "List-schedule (ims) refused: "
		     "ims-dependence-distance-unproven at uid=%d in bb %d\n",
		     INSN_UID (nodes[0].insn), bb->index);
	return false;
      }

  out->resmii = rvtt_timing::resmii (prob);
  out->recmii = rvtt_timing::recmii (prob);
  out->mii = out->resmii > out->recmii ? out->resmii : out->recmii;
  if (dump_file)
    fprintf (dump_file, "List-schedule (ims) region: bb %d nodes=%u "
	     "ResMII=%d RecMII=%d MII=%d\n",
	     bb->index, n, out->resmii, out->recmii, out->mii);
  if (out->recmii < 0 || out->mii >= max_ii)
    {
      rvtt_refuse (RVTT_REF_IMS_NO_II_DECREASE, dump_file,
		   "List-schedule (ims) refused: ims-no-ii-decrease at "
		   "uid=%d in bb %d (MII %d >= II %d)\n",
		   INSN_UID (nodes[0].insn), bb->index, out->mii, max_ii);
      return false;
    }

  int budget = riscv_tt_ims_budget > 0 ? (int) riscv_tt_ims_budget
					: 8 * (int) n;
  rvtt_timing::mod_placement pl
    = rvtt_timing::ims_schedule (prob, out->mii, max_ii - 1, budget);
  if (!pl.scheduled)
    {
      rvtt_refuse (RVTT_REF_IMS_BUDGET_EXHAUSTED, dump_file,
		   "List-schedule (ims) refused: ims-budget-exhausted at "
		   "uid=%d in bb %d (MII %d, bound %d, budget %d)\n",
		   INSN_UID (nodes[0].insn), bb->index, out->mii, max_ii,
		   budget);
      return false;
    }
  out->place_ii = pl.ii;
  out->kmin = rvtt_timing::mve_kmin (prob, pl);
  out->demand = rvtt_timing::mve_live_demand (prob, pl);

  if (out->kmin > 1)
    {
      /* The value lifetimes exceed the II, so overlapping them would
	 need kmin kernel copies, which this path does not create.  The
	 steady state would hold DEMAND live value copies; refuse if they
	 cannot fit the register file net of the loop-live invariants
	 (live into the row and defined by no region node), or if the
	 region rename already ran out of free LREGs.  Otherwise the
	 flat order below is still offered; the wrapped acceptance model
	 judges it without the overlap.  */
      unsigned invariants = 0;
      for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
	{
	  if (!REGNO_REG_SET_P (df_get_live_in (bb), r))
	    continue;
	  bool defined = false;
	  for (unsigned i = 0; i != n && !defined; ++i)
	    defined = TEST_HARD_REG_BIT (nodes[i].raw_defs, r);
	  if (!defined)
	    ++invariants;
	}
      unsigned capacity = rvtt_pressure_capacity ();
      unsigned net = capacity > invariants ? capacity - invariants : 0;
      if (rename_exhausted || out->demand > net)
	rvtt_refuse (RVTT_REF_MVE_RENAME_EXHAUSTED, dump_file,
		     "List-schedule (ims) refused: mve-rename-exhausted "
		     "at uid=%d in bb %d (kmin=%d demand=%u capacity=%u "
		     "invariants=%u%s; flat order still offered)\n",
		     INSN_UID (nodes[0].insn), bb->index, out->kmin,
		     out->demand, capacity, invariants,
		     rename_exhausted ? " rename-search-exhausted" : "");
      else if (riscv_tt_opt_mve_expand)
	/* Only the cross-row pairing can perform the expansion; a region
	   of a barrier-split or non-counted row has no valid kernel copy.
	   The flat order is still offered.  */
	rvtt_refuse (RVTT_REF_MVE_EXPAND_ROW_NOT_COUNTED_KERNEL, dump_file,
		     "List-schedule (ims) refused: "
		     "mve-expand-row-not-counted-kernel at uid=%d in bb %d "
		     "(kmin=%d demand=%u invariants=%u fits; flat order "
		     "still offered)\n",
		     INSN_UID (nodes[0].insn), bb->index, out->kmin,
		     out->demand, invariants);
      else if (dump_file)
	fprintf (dump_file, "List-schedule (ims) MVE owed: bb %d kmin=%d "
		 "demand=%u invariants=%u (fits; kernel unroll is the "
		 "staged follow-up, flat order offered)\n",
		 bb->index, out->kmin, out->demand, invariants);
    }

  /* Committed order: placement slots ascending, original index on
     ties.  Intra-iteration edges force strictly increasing slots, so
     the order respects every dependence; the check below verifies
     it.  */
  out->order.clear ();
  for (unsigned i = 0; i != n; ++i)
    out->order.push_back ((int) i);
  for (unsigned i = 1; i < n; ++i)
    {
      int v = out->order[i];
      unsigned j = i;
      while (j > 0 && (pl.sigma[out->order[j - 1]] > pl.sigma[v]
		       || (pl.sigma[out->order[j - 1]] == pl.sigma[v]
			   && out->order[j - 1] > v)))
	{
	  out->order[j] = out->order[j - 1];
	  --j;
	}
      out->order[j] = v;
    }
  std::vector<int> pos (n, 0);
  for (unsigned k = 0; k != n; ++k)
    pos[out->order[k]] = (int) k;
  for (unsigned i = 0; i != n; ++i)
    for (unsigned j = i + 1; j != n; ++j)
      if (ls_dependence (nodes[i], nodes[j]) && pos[i] > pos[j])
	{
	  rvtt_refuse (RVTT_REF_IMS_ORDER_HAZARD, dump_file,
		       "List-schedule (ims) refused: ims-order-hazard at "
		       "uid=%d/uid=%d in bb %d\n",
		       INSN_UID (nodes[i].insn), INSN_UID (nodes[j].insn),
		       bb->index);
	  return false;
	}
  if (dump_file)
    fprintf (dump_file, "List-schedule (ims) placed: bb %d place-II=%d "
	     "kmin=%d demand=%u\n",
	     bb->index, out->place_ii, out->kmin, out->demand);
  return true;
}

/* Under -mtt-tensix-optimize-ims, refuse a self-loop row that has an
   issued word with a result (or a next-slot stall) but no known latency
   of at most one.  Words without a result (stores, pure CC writers) are
   fine: their latency never weights an edge.  */

static bool
ls_ims_row_audited_p (basic_block bb)
{
  bool ok = true;
  for (rtx_insn *insn = BB_HEAD (bb); insn != NEXT_INSN (BB_END (bb));
       insn = NEXT_INSN (insn))
    {
      if (!NONDEBUG_INSN_P (insn) || GET_CODE (insn) != INSN
	  || recog_memoized (insn) < 0
	  || get_attr_type (insn) != TYPE_TENSIX
	  || !get_attr_length (insn))
	continue;
      xtt_effect_set e = rvtt_insn_effects (insn);
      if (e.opaque)
	continue;	/* refused at the row level */
      if (!e.lreg_write && !e.next_slot_stall)
	continue;
      int lat = audited_latency (insn);
      if (lat < 0 || lat > 1)
	{
	  rvtt_refuse (RVTT_REF_IMS_UNAUDITED_LATENCY, dump_file,
		       "List-schedule (ims) refused: ims-unaudited-latency "
		       "uid=%d in bb %d\n", INSN_UID (insn), bb->index);
	  ok = false;
	}
    }
  return ok;
}

/* Cyclic scheduling of the single region of a self-loop row.  As
   ls_schedule_region, but the acceptance test is a strict steady-state II
   decrease; the pad-site check still applies.  */

static bool
ls_schedule_region_cyclic (basic_block bb, std::vector<ls_node> &nodes,
			   rtx_insn *anchor,
			   std::vector<basic_block> &visited)
{
  unsigned n = nodes.size ();
  for (unsigned i = 0; i != n; ++i)
    {
      nodes[i].entry_pin = 0;
      nodes[i].pin_to_baseline = false;
    }

  /* Check data and baseline come from the original code, before any
     rename.  */
  unsigned pads_before = ls_pad_sites (visited, bb, nodes);
  std::vector<int> base_order (n);
  for (unsigned i = 0; i != n; ++i)
    base_order[i] = i;
  int base_ii = ls_cyclic_ii (nodes, base_order);

  /* Break storage-induced false recurrences first; the renames are
     undone on refusal.  */
  std::vector<ls_rename> renames;
  bool rename_exhausted = false;
  ls_cyclic_rename_collisions (bb, nodes, &renames, nullptr, nullptr,
			       &rename_exhausted);

  /* Candidate orders, all judged by the same wrapped model: the list
     order (round-interleave) and the IMS order.  */
  std::vector<int> order;
  int cand_ii = INT_MAX;
  bool used_ims = false;
  if (riscv_tt_opt_round_interleave)
    {
      order = ls_list_order (nodes);
      cand_ii = ls_cyclic_ii (nodes, order);
    }
  if (riscv_tt_opt_ims)
    {
      ls_ims_candidate ims;
      if (ls_ims_order (bb, nodes, base_ii, rename_exhausted, &ims))
	{
	  int ims_ii = ls_cyclic_ii (nodes, ims.order);
	  if (dump_file)
	    fprintf (dump_file, "List-schedule (ims) candidate: bb %d "
		     "modeled II %d (place-II %d MII %d)\n",
		     bb->index, ims_ii, ims.place_ii, ims.mii);
	  if (ims_ii < cand_ii)
	    {
	      order = ims.order;
	      cand_ii = ims_ii;
	      used_ims = true;
	    }
	}
    }

  if (order.empty () || cand_ii >= base_ii)
    {
      ls_undo_renames (renames);
      if (riscv_tt_opt_ims && !order.empty ())
	rvtt_refuse (RVTT_REF_IMS_NO_II_DECREASE, dump_file,
		     "List-schedule (ims) refused: ims-no-ii-decrease at "
		     "uid=%d in bb %d (%d -> %d)\n",
		     INSN_UID (nodes[0].insn), bb->index, base_ii, cand_ii);
      if (dump_file)
	fprintf (dump_file, "List-schedule refused: no modeled "
		 "steady-state II decrease in bb %d cyclic region at "
		 "uid=%d (%d -> %d)\n",
		 bb->index, INSN_UID (nodes[0].insn), base_ii,
		 cand_ii == INT_MAX ? base_ii : cand_ii);
      return false;
    }

  /* Record for exact restoration, debug insns included.  */
  std::vector<rtx_insn *> chain;
  for (rtx_insn *w = NEXT_INSN (anchor);; w = NEXT_INSN (w))
    {
      if (INSN_P (w))
	chain.push_back (w);
      if (w == nodes[n - 1].insn)
	break;
    }

  rtx_insn *after = anchor;
  for (unsigned k = 0; k != n; ++k)
    {
      rtx_insn *insn = nodes[order[k]].insn;
      if (PREV_INSN (insn) != after)
	reorder_insns (insn, insn, after);
      after = insn;
    }

  unsigned pads_after = ls_pad_sites (visited, bb, nodes);
  if (pads_after > pads_before)
    {
      after = anchor;
      for (rtx_insn *insn : chain)
	{
	  if (PREV_INSN (insn) != after)
	    reorder_insns (insn, insn, after);
	  after = insn;
	}
      ls_undo_renames (renames);
      rvtt_refuse (RVTT_REF_PAD_SITE, dump_file,
		   "List-schedule refused: pad-site increase, "
		   "restored bb %d cyclic region at uid=%d\n",
		   bb->index, INSN_UID (nodes[0].insn));
      return false;
    }

  if (dump_file)
    {
      fprintf (dump_file, "List-schedule (%s cyclic): "
	       "bb %d nodes=%u II %d -> %d renames=%zu target=%s\n",
	       used_ims ? "ims" : "round-interleave",
	       bb->index, n, base_ii, cand_ii, renames.size (),
	       TARGET_XTT_TENSIX_WH ? "wh" : "bh");
      for (unsigned k = 0; k != n; ++k)
	fprintf (dump_file, "List-schedule slot-order=%u uid=%d\n",
		 k, INSN_UID (nodes[order[k]].insn));
    }
  return true;
}


/* Return the entry producer of a region starting at FIRST: the nearest
   preceding instruction that the DYNAMIC pad probe (find_next_insn)
   treats as adjacent to FIRST.  The probe skips non-Tensix insns,
   USE/CLOBBER markers and non-dependent zero-length patterns, so a scalar
   insn in between does not break the adjacency; this walk skips exactly
   the same insns.  */

static rtx_insn *
ls_entry_producer (basic_block bb, rtx_insn *first)
{
  for (rtx_insn *w = PREV_INSN (first); w && w != PREV_INSN (BB_HEAD (bb));
       w = PREV_INSN (w))
    {
      if (!NONDEBUG_INSN_P (w))
	continue;
      if (GET_CODE (w) != INSN)
	/* Stop at a jump or call.  find_next_insn walks through a call,
	   so a region right after a call conservatively sees no entry
	   producer.  */
	return nullptr;
      rtx pat = PATTERN (w);
      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	continue;
      if (recog_memoized (w) < 0)
	return nullptr;
      if (get_attr_type (w) != TYPE_TENSIX)
	continue;		/* scalar insns are skipped by the probe */
      if (!get_attr_length (w))
	continue;		/* zero-length marker */
      return w;
    }
  return nullptr;
}

/* One collected candidate region of a block.  */

struct ls_region
{
  std::vector<ls_node> nodes;
  rtx_insn *anchor;
  rtx_insn *entry_producer;
  HARD_REG_SET unaudited_defs;	/* entry producer's defs when its
				   latency is unknown or above one */
  std::vector<int> signature;	/* insn codes, for the repeat deferral */
};

/* Isomorphic-pair scheduling (round-interleave): apply R1's chosen
   permutation to R2 so the copies stay textually identical.  The
   dependence matrices have been checked equal; each region is judged by
   its own boundary model, and a refusal on R2 restores R1.  */

static void
ls_schedule_iso_pair (basic_block bb, ls_region &r1, ls_region &r2,
		      std::vector<basic_block> &visited)
{
  unsigned n = r1.nodes.size ();
  if (r2.nodes.size () != n)
    return;	/* equal signatures imply equal sizes */

  for (unsigned i = 0; i != n; ++i)
    for (unsigned j = i + 1; j != n; ++j)
      if (ls_dependence (r1.nodes[i], r1.nodes[j])
	  != ls_dependence (r2.nodes[i], r2.nodes[j]))
	{
	  rvtt_refuse (RVTT_REF_COPIES_NOT_DATAFLOW_ISOMORPHIC, dump_file,
		       "List-schedule refused: "
		       "copies-not-dataflow-isomorphic at uid=%d/uid=%d "
		       "in bb %d (repeated-row pair keeps its deferral)\n",
		       INSN_UID (r1.nodes[0].insn),
		       INSN_UID (r2.nodes[0].insn), bb->index);
	  return;
	}

  /* Record the first region before committing it, so a refusal on
     the second region can undo the pair.  */
  std::vector<rtx_insn *> chain1;
  for (rtx_insn *w = NEXT_INSN (r1.anchor);; w = NEXT_INSN (w))
    {
      if (INSN_P (w))
	chain1.push_back (w);
      if (w == r1.nodes[n - 1].insn)
	break;
    }

  std::vector<int> order;
  if (!ls_schedule_region (bb, r1.nodes, r1.anchor, r1.entry_producer,
			   next_issued_insn (bb, r1.nodes.back ().insn),
			   r1.unaudited_defs, visited, nullptr, &order))
    return;	/* first copy refused; nothing moved */

  if (!ls_schedule_region (bb, r2.nodes, r2.anchor, r2.entry_producer,
			   next_issued_insn (bb, r2.nodes.back ().insn),
			   r2.unaudited_defs, visited, &order, nullptr))
    {
      /* Restore the first region.  */
      rtx_insn *after = r1.anchor;
      for (rtx_insn *insn : chain1)
	{
	  if (PREV_INSN (insn) != after)
	    reorder_insns (insn, insn, after);
	  after = insn;
	}
      rvtt_refuse (RVTT_REF_ISO_PAIR, dump_file,
		   "List-schedule refused: iso-pair sibling at "
		   "uid=%d would not improve, restored pair in bb %d\n",
		   INSN_UID (r2.nodes[0].insn), bb->index);
      return;
    }

  if (dump_file)
    fprintf (dump_file, "List-schedule (round-interleave iso-pair): "
	     "bb %d regions at uid=%d/uid=%d share one permutation "
	     "(isomorphism preserved)\n",
	     bb->index, INSN_UID (r1.nodes[0].insn),
	     INSN_UID (r2.nodes[0].insn));
}

/* Cross-row pairing (-mtt-tensix-optimize-crossrow-pairing).

   Fuse two consecutive iterations of a capturable single-row Dst loop
   into one doubled row whose halves interleave.  The loop keeps the
   counted replay-capture shape, with half as many launches, while the
   interleave fills dependency stalls the single row cannot (for example
   a mad->setcc adjacency when every possible filler is CC-predicated).

   Accepted shape (anything else is refused and left unchanged):

     row:   one constant-address, no-increment Dst load first, pure or CC
	    words, and one matching Dst store last; every word has known
	    effects, a latency of at most one, one issue slot, only SFPU
	    register references and a fixed replay encoding;
     CC:    flat atoms only, each opening at a CC writer and closing at
	    the all-lanes SFPENCC restore (cc_write_all_lanes), with no Dst
	    or RWC effect inside.  All lanes are enabled between atoms and
	    at loop entry, checked by a backward walk to the nearest CC
	    writer or to the function entry (where gimple-rvtt-cc.cc
	    guarantees all lanes are enabled);
     step:  one trailing TTINCRWC (0, d, 0, 0) row separator, the only RWC
	    effect in the loop;
     ctrl:  the canonical countdown (reg += -1; if (reg != 0) backedge),
	    with the register used nowhere else and seeded with an even
	    positive constant.

   Transformation (a single transaction):

     1. row B is a copy of row A, emitted after it;
     2. B's Dst addresses are rebased from A to A + d, the separator
	advance doubles to 2d, and the countdown step becomes -2, so the
	pair touches exactly the Dst rows and RWC positions of the two
	original iterations;
     3. row-B webs that the allocator packed onto row-A registers are
	renamed to dead LREGs, restricted to webs rooted where all lanes
	are enabled;
     4. the pure spans of both rows are list-scheduled together; CC atoms
	stay contiguous and in order (atom A before atom B), and stores
	keep their order;
     5. the result is kept only if the modeled steady-state II of the
	doubled row strictly decreases, no pad site is added, and the
	doubled row still fits the replay buffer
	(XTT_DELIVERY_CAPTURE_SLOTS).

   Blackhole only, since the latency model is for Blackhole.  */

static basic_block rotation_dedicated_preheader (basic_block bb);

struct crp_loop
{
  basic_block bb;
  std::vector<ls_node> nodes;			/* row words in order */
  std::vector<std::pair<unsigned, unsigned> > atoms; /* inclusive */
  unsigned load = ~0u;
  unsigned store = ~0u;
  rtx_insn *separator = nullptr;
  rtx_insn *counter = nullptr;
  rtx_insn *jump = nullptr;
  unsigned counter_regno = ~0u;
  HOST_WIDE_INT trips = 0;
  HOST_WIDE_INT dst_addr = 0;
  HOST_WIDE_INT dst_step = 0;
};

/* Report the cross-row pairing refusal WHY in BB, naming INSN if
   given.  Return false.  */

static bool
crp_refuse (basic_block bb, const char *why, rtx_insn *insn = nullptr)
{
  if (dump_file)
    {
      rvtt_refuse_by_name (why, dump_file,
			   "Crossrow pairing refused: %s", why);
      if (insn)
	fprintf (dump_file, " (uid=%d)", INSN_UID (insn));
      fprintf (dump_file, " in bb %d\n", bb->index);
    }
  return false;
}

/* Admit a row word into the pairing's scheduling model, which is the
   list scheduler's (latencies of at most one, LREG dependence edges).  CC
   words keep their LREG uses for RAW ordering; their CC effect is
   protected by keeping the atom contiguous and in order.  */

static bool
crp_node (basic_block bb, rtx_insn *insn, ls_node *node)
{
  if (!issued_tensix_p (insn) || JUMP_P (insn)
      || get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
    return crp_refuse (bb, "crossrow-pairing-foreign-word", insn);
  if (get_attr_xtt_replay (insn) != XTT_REPLAY_SAFE
      || !rvtt_fixed_replay_rtx_p (PATTERN (insn)))
    return crp_refuse (bb, "crossrow-pairing-noncapturable-word", insn);
  xtt_effect_set e = rvtt_insn_effects (insn);
  /* With the stall-words option, next-slot stall words (the SFPSWAP
     family) are admitted with their result latency, and the stall is
     charged as two issue slots in the II model (one extra slot per
     occurrence, see rvtt-cost.md), both in the baseline and in every
     candidate.  audited_latency () still returns -1 for them, so the
     fill phases never move them.  */
  bool stall_word = e.next_slot_stall
    && riscv_tt_opt_crossrow_pairing_stall_words > 0;
  if (e.opaque || e.config_dests_written || e.config_dests_read
      || e.addr_mod_slot_write || (e.next_slot_stall && !stall_word)
      || get_attr_xtt_delay (insn) == XTT_DELAY_STATIC)
    return crp_refuse (bb, "crossrow-pairing-effect-unproven", insn);
  /* Every DF reference must be an SFPU register; a scalar dependence
     would not be tracked.  */
  for (df_ref ref = DF_INSN_USES (insn); ref; ref = DF_REF_NEXT_LOC (ref))
    if (DF_REF_REGNO (ref) >= FIRST_PSEUDO_REGISTER
	|| !SFPU_REG_P (DF_REF_REGNO (ref)))
      return crp_refuse (bb, "crossrow-pairing-scalar-dependence", insn);
  for (df_ref ref = DF_INSN_DEFS (insn); ref; ref = DF_REF_NEXT_LOC (ref))
    if (DF_REF_REGNO (ref) >= FIRST_PSEUDO_REGISTER
	|| !SFPU_REG_P (DF_REF_REGNO (ref)))
      return crp_refuse (bb, "crossrow-pairing-scalar-dependence", insn);
  /* For a stall word, read the attribute directly since
     audited_latency () returns -1; an unknown latency is still refused
     by the range check below.  */
  node->lat = stall_word ? e.result_latency : audited_latency (insn);
  /* Pure flag writers have no LREG result; their ordering is that of
     the atom.  */
  if ((node->lat < 0 || node->lat > 1) && e.cc_write && !e.lreg_write
      && rvtt_macro::cc_visibility_lag () == 1)
    node->lat = 0;
  bool regs_ok = collect_sfpu_regs (insn, &node->regs);
  if (!regs_ok && (e.cc_write || e.dst_mem_write) && !e.lreg_write)
    {
      /* CC and store words without an LREG destination are not
	 accepted by collect_sfpu_regs; the SFPU-only check above
	 already excluded scalar references, so keep their LREG uses
	 for RAW ordering.  */
      sfpu_reg_refs (insn, &node->regs);
      regs_ok = true;
    }
  if (node->lat < 0 || node->lat > 1 || !regs_ok)
    return crp_refuse (bb, "crossrow-pairing-latency-or-lreg-unproven", insn);
  node->raw_defs = node->regs.defs;
  node->insn = insn;
  node->words = get_attr_length (insn) / 4;
  if (node->words != 1)
    return crp_refuse (bb, "crossrow-pairing-word-width-unproven", insn);
  /* The next-slot stall costs issue time, not a stream word: the node
     takes two slots in the timing model but counts as one recorded
     word against the capture budget.  */
  if (stall_word)
    node->words = 2;
  node->entry_pin = 0;
  node->pin_to_baseline = false;
  return true;
}

/* Return true if all lanes are enabled at the loop's entry: walking
   backwards from the loop's preheader through single-predecessor blocks,
   the nearest CC writer must be the all-lanes restore, or the walk
   reaches the function entry.  Anything else is refused.  */

static bool
crp_entry_all_lanes_p (basic_block bb, basic_block preheader)
{
  basic_block cur = preheader;
  while (true)
    {
      for (rtx_insn *insn = BB_END (cur);
	   insn && insn != PREV_INSN (BB_HEAD (cur));
	   insn = PREV_INSN (insn))
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0)
	    return crp_refuse (bb, "crossrow-pairing-entry-cc-unproven", insn);
	  if (GET_CODE (insn) != INSN
	      || GET_CODE (PATTERN (insn)) == USE
	      || GET_CODE (PATTERN (insn)) == CLOBBER)
	    continue;
	  if (recog_memoized (insn) < 0
	      || get_attr_type (insn) != TYPE_TENSIX)
	    continue;	/* scalar insns do not touch lane state */
	  if (!get_attr_length (insn))
	    continue;	/* zero-length marker */
	  xtt_effect_set e = rvtt_insn_effects (insn);
	  if (e.opaque)
	    return crp_refuse (bb, "crossrow-pairing-entry-cc-unproven", insn);
	  if (e.cc_write_all_lanes)
	    return true;
	  if (e.cc_write)
	    return crp_refuse (bb, "crossrow-pairing-entry-cc-unproven", insn);
	}
      if (!single_pred_p (cur))
	return crp_refuse (bb, "crossrow-pairing-entry-cc-unproven");
      cur = single_pred (cur);
      if (cur == ENTRY_BLOCK_PTR_FOR_FN (cfun))
	return true;
    }
}

/* Check the structure of the whole loop.  Refuse silently only when BB
   is not a self-loop at all.  */

static bool
crp_admit_loop (basic_block bb, crp_loop *lp)
{
  bool self = false;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (e->dest == bb)
      self = true;
  if (!self)
    return false;
  if (!TARGET_XTT_TENSIX_BH)
    return crp_refuse (bb, "crossrow-pairing-bh-only");
  if (EDGE_COUNT (bb->succs) != 2 || EDGE_COUNT (bb->preds) != 2)
    return crp_refuse (bb, "crossrow-pairing-row-shape");

  lp->bb = bb;
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (JUMP_P (insn))
	{
	  if (insn != BB_END (bb))
	    return crp_refuse (bb, "crossrow-pairing-row-shape", insn);
	  lp->jump = insn;
	  continue;
	}
      if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0
	  || contains_mem_rtx_p (PATTERN (insn)))
	return crp_refuse (bb, "crossrow-pairing-foreign-word", insn);
      if (GET_CODE (PATTERN (insn)) == USE
	  || GET_CODE (PATTERN (insn)) == CLOBBER)
	return crp_refuse (bb, "crossrow-pairing-bookkeeping-word", insn);
      if (recog_memoized (insn) >= 0 && get_attr_type (insn) == TYPE_TENSIX)
	{
	  if (!get_attr_length (insn))
	    return crp_refuse (bb, "crossrow-pairing-ghost-word", insn);
	  if (lp->counter)
	    return crp_refuse (bb, "crossrow-pairing-counter-position", insn);
	  xtt_effect_set eff = rvtt_insn_effects (insn);
	  if (!eff.opaque && eff.rwc.kind == xtt_rwc_effect_t::INC
	      && !eff.lreg_read && !eff.lreg_write
	      && !eff.cc_read && !eff.cc_write
	      && !eff.dst_mem_read && !eff.dst_mem_write)
	    {
	      /* Typed row separator: exactly one, trailing.  */
	      if (lp->separator || lp->nodes.empty ()
		  || eff.rwc.dst_delta <= 0 || eff.rwc.cr_delta != 0)
		return crp_refuse (bb, "crossrow-pairing-row-step-shape",
				   insn);
	      lp->separator = insn;
	      lp->dst_step = eff.rwc.dst_delta;
	      continue;
	    }
	  if (lp->separator)
	    return crp_refuse (bb, "crossrow-pairing-word-after-row-step",
			       insn);
	  ls_node node;
	  if (!crp_node (bb, insn, &node))
	    return false;
	  node.orig = lp->nodes.size ();
	  lp->nodes.push_back (node);
	  continue;
	}
      /* Scalar insns: accept only the canonical countdown, after the
	 separator.  */
      if (lp->counter || !lp->separator)
	return crp_refuse (bb, "crossrow-pairing-counter-shape", insn);
      rtx set = single_set (insn);
      if (!set || !REG_P (SET_DEST (set))
	  || SFPU_REG_P (REGNO (SET_DEST (set)))
	  || GET_CODE (SET_SRC (set)) != PLUS
	  || !rtx_equal_p (XEXP (SET_SRC (set), 0), SET_DEST (set))
	  || !CONST_INT_P (XEXP (SET_SRC (set), 1))
	  || INTVAL (XEXP (SET_SRC (set), 1)) != -1)
	return crp_refuse (bb, "crossrow-pairing-counter-shape", insn);
      lp->counter = insn;
      lp->counter_regno = REGNO (SET_DEST (set));
    }

  if (!lp->separator || !lp->counter || !lp->jump)
    return crp_refuse (bb, "crossrow-pairing-row-shape");
  if (lp->nodes.size () < XTT_CROSSROW_MIN_ROW_WORDS)
    return crp_refuse (bb, "crossrow-pairing-row-too-short");
  if (2 * lp->nodes.size () > (unsigned) XTT_DELIVERY_CAPTURE_SLOTS)
    return crp_refuse (bb, "crossrow-pairing-capture-budget");

  /* The backedge condition: if (counter != 0) goto header.  */
  rtx jset = single_set (lp->jump);
  rtx cond = jset ? SET_SRC (jset) : nullptr;
  if (!cond || GET_CODE (cond) != IF_THEN_ELSE
      || GET_CODE (XEXP (cond, 0)) != NE
      || !REG_P (XEXP (XEXP (cond, 0), 0))
      || REGNO (XEXP (XEXP (cond, 0), 0)) != lp->counter_regno
      || XEXP (XEXP (cond, 0), 1) != const0_rtx
      || GET_CODE (XEXP (cond, 1)) != LABEL_REF
      || XEXP (cond, 2) != pc_rtx)
    return crp_refuse (bb, "crossrow-pairing-counter-shape", lp->jump);

  /* Row shape: one load first, one store last, no other Dst or RWC
     traffic, flat atoms closed by the all-lanes restore.  */
  bool cc_open = false;
  unsigned cc_begin = 0;
  unsigned loads = 0, stores = 0;
  for (unsigned i = 0; i != lp->nodes.size (); ++i)
    {
      xtt_effect_set eff = rvtt_insn_effects (lp->nodes[i].insn);
      if (eff.rwc.kind != xtt_rwc_effect_t::NONE)
	return crp_refuse (bb, "crossrow-pairing-rwc-inside-row",
			   lp->nodes[i].insn);
      if (eff.cc_write_all_lanes)
	{
	  if (!cc_open)
	    return crp_refuse (bb, "crossrow-pairing-unmatched-restore",
			       lp->nodes[i].insn);
	  lp->atoms.push_back ({cc_begin, i});
	  cc_open = false;
	}
      else if (eff.cc_write && !cc_open)
	{
	  cc_open = true;
	  cc_begin = i;
	}
      if (eff.dst_mem_read || eff.dst_mem_write)
	{
	  if (cc_open)
	    return crp_refuse (bb, "crossrow-pairing-dst-in-cc-window",
			       lp->nodes[i].insn);
	  rtx addr, mode, am;
	  if (!rvtt_dst_access_operands (lp->nodes[i].insn, eff, &addr,
					 &mode, &am)
	      || !CONST_INT_P (addr) || !CONST_INT_P (mode)
	      || !CONST_INT_P (am)
	      || INTVAL (am) != rvtt_no_increment_address_mode ())
	    return crp_refuse (bb, "crossrow-pairing-dst-operands-unproven",
			       lp->nodes[i].insn);
	  if (eff.dst_mem_read)
	    lp->load = i, ++loads;
	  if (eff.dst_mem_write)
	    lp->store = i, ++stores;
	}
    }
  if (cc_open)
    return crp_refuse (bb, "crossrow-pairing-unclosed-cc-window");
  if (loads != 1 || stores != 1 || lp->load != 0
      || lp->store + 1 != lp->nodes.size ())
    return crp_refuse (bb, "crossrow-pairing-row-shape");

  /* Both accesses use one constant address A; the copy uses A + d,
     where d is the separator's per-iteration advance (2 address units,
     one row).  Check alignment and range so the footprints are
     disjoint and the rebased address is encodable.  */
  xtt_effect_set le = rvtt_insn_effects (lp->nodes[lp->load].insn);
  xtt_effect_set se = rvtt_insn_effects (lp->nodes[lp->store].insn);
  rtx la, lm, lam, sa, sm, sam;
  HOST_WIDE_INT addr_limit = 8191;	/* BH Dst address range */
  if (!rvtt_dst_access_operands (lp->nodes[lp->load].insn, le, &la, &lm,
				 &lam)
      || !rvtt_dst_access_operands (lp->nodes[lp->store].insn, se, &sa,
				    &sm, &sam)
      || INTVAL (la) != INTVAL (sa)
      || lp->dst_step != 2
      || (INTVAL (la) & 3) || INTVAL (la) < 0
      || INTVAL (la) > addr_limit - lp->dst_step)
    return crp_refuse (bb, "crossrow-pairing-dst-disjointness-unproven");
  lp->dst_addr = INTVAL (la);

  /* The preheader must seed the countdown with an even positive
     constant, so that a step of -2 still reaches zero exactly, and the
     register must be used nowhere else in the row.  */
  basic_block preheader = rotation_dedicated_preheader (bb);
  if (!preheader)
    return crp_refuse (bb, "crossrow-pairing-no-dedicated-preheader");
  rtx_insn *init = nullptr;
  for (rtx_insn *w = BB_END (preheader);
       w && w != PREV_INSN (BB_HEAD (preheader)); w = PREV_INSN (w))
    {
      if (!NONDEBUG_INSN_P (w))
	continue;
      if (CALL_P (w) || asm_noperands (PATTERN (w)) >= 0)
	return crp_refuse (bb, "crossrow-pairing-counter-init-unproven", w);
      if (reg_set_p (SET_DEST (single_set (lp->counter)), w))
	{
	  init = w;
	  break;
	}
    }
  rtx iset = init ? single_set (init) : nullptr;
  if (!iset || !REG_P (SET_DEST (iset))
      || REGNO (SET_DEST (iset)) != lp->counter_regno
      || !CONST_INT_P (SET_SRC (iset)))
    return crp_refuse (bb, "crossrow-pairing-counter-init-unproven");
  lp->trips = INTVAL (SET_SRC (iset));
  if (lp->trips < 2)
    return crp_refuse (bb, "crossrow-pairing-trips-unproven");
  if (lp->trips & 1)
    return crp_refuse (bb, "crossrow-pairing-trips-odd");

  /* All lanes enabled at loop entry.  */
  if (!crp_entry_all_lanes_p (bb, preheader))
    return false;

  return true;
}

/* Queue a replacement of the Dst address of a load/store copy.  */

static void
crp_queue_dst_rebase (rtx_insn *insn, HOST_WIDE_INT value)
{
  int code = recog_memoized (insn);
  int addr_pos = code == CODE_FOR_rvtt_sfpload_lv_int ? 4 : 3;
  gcc_assert (code == CODE_FOR_rvtt_sfpload_lv_int
	      || code == CODE_FOR_rvtt_sfpstore_int);
  extract_insn (insn);
  validate_change (insn, recog_data.operand_loc[addr_pos], GEN_INT (value),
		   true);
}

/* Candidate order construction: a dependence-respecting list schedule
   over items, where each CC atom is one indivisible item (its words stay
   contiguous and in order) and every other row word is its own item.
   Dependences use the aggregated register sets and point in the original
   two-iteration order, so a shared, unrenamed web serializes exactly as
   the two original iterations would.  */

struct crp_item
{
  std::vector<unsigned> members;	/* indices into ALL, in order */
  HARD_REG_SET uses, raw_defs;
  int words;
  int lat;				/* conservative max member latency */
  unsigned orig;			/* first member's original index */
};

/* Return true if super-item C depends on the earlier super-item P
   (RAW, WAW or WAR overlap).  Marker references are in uses only, so pure
   readers never conflict.  */

static bool
crp_item_dep (const crp_item &p, const crp_item &c)
{
  return hard_reg_set_intersect_p (p.raw_defs, c.uses)
    || hard_reg_set_intersect_p (p.raw_defs, c.raw_defs)
    || hard_reg_set_intersect_p (p.uses, c.raw_defs);
}

/* Compute the candidate emission order for the doubled body ALL.  GROUP
   maps each node to an atom instance (a run of one non-negative id is one
   item; -1 nodes stand alone).  Dependences follow original order, so the
   result is legal by construction.  */

static std::vector<rtx_insn *>
crp_candidate_order (const std::vector<ls_node> &all,
		     const std::vector<int> &group)
{
  /* Build the items in original order.  */
  std::vector<crp_item> items;
  unsigned total = all.size ();
  unsigned i = 0;
  while (i != total)
    {
      crp_item it;
      CLEAR_HARD_REG_SET (it.uses);
      CLEAR_HARD_REG_SET (it.raw_defs);
      it.words = 0;
      it.lat = 0;
      it.orig = i;
      unsigned end = i + 1;
      if (group[i] >= 0)
	while (end != total && group[end] == group[i])
	  ++end;
      for (unsigned k = i; k != end; ++k)
	{
	  const ls_node &nd = all[k];
	  it.members.push_back (k);
	  it.uses |= nd.regs.uses;
	  it.raw_defs |= nd.raw_defs;
	  it.words += nd.words;
	  if (nd.lat > it.lat)
	    it.lat = nd.lat;
	}
      items.push_back (std::move (it));
      i = end;
    }

  /* Greedy list schedule over the items: modeled issue time from the
     item dependences (latency-weighted for RAW/WAW, issue order for
     WAR), earliest ready first, original order on ties.

     With the stall-words option, ready items are instead taken
     critical-path first (as in ls_list_order).  Draining row A first
     can leave row B's SFPMUL->SFPSWAP delay at the end of the body,
     which costs an SFPNOP and can push the doubled row past the capture
     budget; critical-path order interleaves the two tails instead.  */
  unsigned m = items.size ();
  bool cp_priority = riscv_tt_opt_crossrow_pairing_stall_words > 0;
  std::vector<long> cp (m, 0);
  if (cp_priority)
    for (unsigned i = m; i--;)
      {
	long best = items[i].words + items[i].lat;
	for (unsigned j = i + 1; j != m; ++j)
	  {
	    if (!crp_item_dep (items[i], items[j]))
	      continue;
	    long via = cp[j] + items[i].words + items[i].lat;
	    if (via > best)
	      best = via;
	  }
	cp[i] = best;
      }
  std::vector<bool> placed (m, false);
  std::vector<int> finish (m, 0);
  std::vector<rtx_insn *> order;
  int t = 0;
  for (unsigned step = 0; step != m; ++step)
    {
      int best = -1;
      int best_ready = INT_MAX;
      for (unsigned i = 0; i != m; ++i)
	{
	  if (placed[i])
	    continue;
	  bool deps_done = true;
	  int ready = 0;
	  for (unsigned j = 0; j != m; ++j)
	    {
	      if (j == i || items[j].orig > items[i].orig)
		continue;
	      if (!crp_item_dep (items[j], items[i]))
		continue;
	      if (!placed[j])
		{
		  deps_done = false;
		  break;
		}
	      if (finish[j] > ready)
		ready = finish[j];
	    }
	  if (!deps_done)
	    continue;
	  bool take;
	  if (best < 0)
	    take = true;
	  else if (cp_priority)
	    {
	      /* Among items ready by the later of the two ready times,
		 prefer the longer remaining critical path; earlier
		 readiness wins only when the earlier item's issue
		 cannot overlap the other's wait.  Ties fall to original
		 order.  */
	      int now = t > best_ready ? t : best_ready;
	      int now_i = t > ready ? t : ready;
	      if (now_i != now)
		take = now_i < now;
	      else
		take = cp[i] > cp[best]
		  || (cp[i] == cp[best] && items[i].orig < items[best].orig);
	    }
	  else
	    take = ready < best_ready
	      || (ready == best_ready && items[i].orig < items[best].orig);
	  if (take)
	    {
	      best = (int) i;
	      best_ready = ready;
	    }
	}
      gcc_assert (best >= 0);
      if (best_ready > t)
	t = best_ready;
      t += items[best].words;
      finish[best] = t + items[best].lat;
      placed[best] = true;
      for (unsigned k : items[best].members)
	order.push_back (all[k].insn);
    }
  return order;
}

/* Return true if every original-order dependence keeps its direction
   in the candidate order.  The construction above guarantees this; the
   check guards against errors in it.  */

static bool
crp_order_legal_p (const std::vector<ls_node> &all,
		   const std::vector<rtx_insn *> &candidate)
{
  std::vector<int> pos (all.size (), -1);
  for (unsigned p = 0; p != candidate.size (); ++p)
    for (unsigned i = 0; i != all.size (); ++i)
      if (all[i].insn == candidate[p])
	pos[i] = (int) p;
  for (unsigned i = 0; i != all.size (); ++i)
    if (pos[i] < 0)
      return false;
  for (unsigned i = 0; i != all.size (); ++i)
    for (unsigned j = i + 1; j != all.size (); ++j)
      if (ls_dependence (all[i], all[j]) && pos[i] > pos[j])
	return false;
  return true;
}

/* Shared-reload dedupe (-mtt-tensix-optimize-crossrow-shared-reload).

   The doubled row contains the copy half's duplicate in-loop constant
   materializations (for example two SFPLOADI groups per half into one
   reload register).  Simply deleting the copy's definitions and keeping
   its consumers would be wrong: in the sequential order, a surviving
   consumer's nearest preceding definition of the shared register is the
   first half's next-epoch materialization, and ls_dependence derives
   value flow from position alone.

   Instead, both halves are split into epoch segments at each definition
   group and re-sequenced epoch by epoch (the first half's segment, then
   the copy's with its definitions deleted).  Every surviving consumer
   then sits between its own epoch's definition group and the next one,
   so position-based dependences give exactly the required constraints:
   RAW from the epoch's definitions into both halves' consumers, and WAR
   from the copy's consumers into the next epoch's definitions.
   crp_order_legal_p and crp_shared_reload_verify check the result.

   The merged order is equivalent to the sequential doubled row because
   (1) the copy half's definition groups are identical to the first
   half's (checked after renaming), so a deleted definition's value is
   the surviving one; (2) the shared register is dead into and out of the
   loop and every consumer follows its group's last member, so each
   consumer reads its epoch's complete value; and (3) the merge only moves
   copy-half words ahead of later-segment first-half words, and such pairs
   may interact through no register other than the shared one.  */

struct crp_shared_reload_info
{
  unsigned reg = ~0u;			/* shared reload register, or ~0u */
  unsigned removed = 0;			/* deleted copy-half def words */
  /* Epoch (1-based) of each participating insn, for
     crp_shared_reload_verify.  */
  std::vector<std::pair<rtx_insn *, unsigned> > def_epoch;
  std::vector<std::pair<rtx_insn *, unsigned> > consumer_epoch;
};

/* Report the shared-reload refusal "crossrow-shared-reload-WHY" for
   register R in BB, naming INSN if given.  */

static void
crp_sr_refuse (basic_block bb, const char *why, unsigned r,
	       rtx_insn *insn = nullptr)
{
  rvtt_refusal_fire_composed ("crossrow-shared-reload", why);
  if (dump_file)
    {
      fprintf (dump_file, "Crossrow shared-reload refused: "
	       "crossrow-shared-reload-%s reg %u", why, r);
      if (insn)
	fprintf (dump_file, " (uid=%d)", INSN_UID (insn));
      fprintf (dump_file, " in bb %d\n", bb->index);
    }
}

/* Return the modeled steady-state II of the candidate order over
   ALL/GROUP, or INT_MAX if no order can be built.  */

static int
crp_model_ii (const std::vector<ls_node> &all, const std::vector<int> &group)
{
  std::vector<rtx_insn *> cand = crp_candidate_order (all, group);
  if (!crp_order_legal_p (all, cand))
    return INT_MAX;
  std::vector<int> idx;
  for (rtx_insn *ci : cand)
    for (unsigned k = 0; k != all.size (); ++k)
      if (all[k].insn == ci)
	{
	  idx.push_back ((int) k);
	  break;
	}
  if (idx.size () != all.size ())
    return INT_MAX;
  return ls_cyclic_ii (all, idx);
}

/* Check the committed order against the epoch assignment: definition
   groups appear whole and in epoch order, and every surviving consumer
   reads its own completed epoch.  This does not use the dependence
   model.  */

static bool
crp_shared_reload_order_sound_p (const crp_shared_reload_info &sr,
				 const std::vector<rtx_insn *> &candidate)
{
  if (sr.reg == ~0u)
    return true;
  unsigned max_epoch = 0;
  for (const auto &p : sr.def_epoch)
    if (p.second > max_epoch)
      max_epoch = p.second;
  std::vector<unsigned> remaining (max_epoch + 1, 0);
  for (const auto &p : sr.def_epoch)
    ++remaining[p.second];
  unsigned cur_epoch = 0;		/* highest def epoch started */
  unsigned consumed_epoch = 0;		/* highest consumer epoch seen */
  unsigned defs_seen = 0, consumers_seen = 0;
  for (rtx_insn *w : candidate)
    {
      unsigned e = 0;
      bool is_def = false, is_consumer = false;
      for (const auto &p : sr.def_epoch)
	if (p.first == w)
	  {
	    e = p.second;
	    is_def = true;
	    break;
	  }
      if (!is_def)
	for (const auto &p : sr.consumer_epoch)
	  if (p.first == w)
	    {
	      e = p.second;
	      is_consumer = true;
	      break;
	    }
      if (is_def)
	{
	  if (e < cur_epoch)
	    return false;		/* groups interleaved */
	  if (e > cur_epoch
	      && (e != cur_epoch + 1 || remaining[cur_epoch] != 0))
	    return false;		/* group started before the
					   previous one completed */
	  if (consumed_epoch >= e)
	    return false;		/* a consumer already read this
					   epoch's value */
	  cur_epoch = e;
	  --remaining[e];
	  ++defs_seen;
	}
      else if (is_consumer)
	{
	  if (cur_epoch != e || remaining[e] != 0)
	    return false;		/* wrong or incomplete epoch */
	  if (e > consumed_epoch)
	    consumed_epoch = e;
	  ++consumers_seen;
	}
    }
  return defs_seen == sr.def_epoch.size ()
	 && consumers_seen == sr.consumer_epoch.size ();
}

/* Deduplicate the doubled row ALL/GROUP (2*N nodes: the first half,
   then the copy).  If every check passes, delete the copy half's
   definition groups, re-sequence ALL/GROUP into the epoch-merged order,
   and fill *OUT for crp_shared_reload_verify.  Otherwise leave the
   pairing unchanged.  */

static void
crp_shared_reload (basic_block bb, const crp_loop &lp,
		   std::vector<ls_node> &all, std::vector<int> &group,
		   std::vector<rtx_insn *> &copies, unsigned n,
		   crp_shared_reload_info *out)
{
  if (all.size () != 2 * n)
    {
      /* Preservation seeds were inserted, so copy word I is no longer
	 at N+I.  */
      crp_sr_refuse (bb, "seeded-row", ~0u);
      return;
    }

  struct sr_cand
  {
    unsigned r;
    std::vector<char> is_def;		/* row index -> group member */
    std::vector<int> epoch_of;		/* row index -> consumer epoch */
    std::vector<unsigned> seg_of;	/* row index -> segment */
    unsigned removed;
    unsigned epochs;
  };
  sr_cand best;
  best.removed = 0;

  for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
    {
      if (fixed_regs[r])
	continue;
      bool any_def = false;
      for (unsigned i = 0; i != n && !any_def; ++i)
	any_def = TEST_HARD_REG_BIT (all[i].raw_defs, r);
      if (!any_def)
	continue;

      sr_cand c;
      c.r = r;
      c.is_def.assign (n, 0);
      c.epoch_of.assign (n, -1);
      c.seg_of.assign (n, 0);

      /* Classify the first half's words against R: definition groups
	 (one constant-only writer plus read-modify-write completions)
	 and consumers, each consumer after its group's last member.  */
      int cur = -1;
      bool open = false;
      bool ok = true;
      for (unsigned i = 0; i != n && ok; ++i)
	{
	  bool d = TEST_HARD_REG_BIT (all[i].raw_defs, r);
	  bool u = TEST_HARD_REG_BIT (all[i].regs.uses, r);
	  if (!d && !u)
	    continue;
	  if (d)
	    {
	      HARD_REG_SET od = all[i].raw_defs;
	      HARD_REG_SET ou = all[i].regs.uses;
	      CLEAR_HARD_REG_BIT (od, r);
	      CLEAR_HARD_REG_BIT (ou, r);
	      if (!hard_reg_set_empty_p (od) || !hard_reg_set_empty_p (ou))
		{
		  /* Not a pure constant-materialization web.  Report it
		     only if groups had already formed; otherwise the
		     register is simply not a reload web.  */
		  if (cur >= 0)
		    crp_sr_refuse (bb, "materialization-shape", r,
				   all[i].insn);
		  ok = false;
		  break;
		}
	      if (!u)
		{
		  ++cur;
		  open = true;
		}
	      else if (cur < 0 || !open)
		{
		  /* A read-modify-write completion outside its group
		     would let a consumer read a partial value.  */
		  crp_sr_refuse (bb, "rmw-outside-group", r, all[i].insn);
		  ok = false;
		  break;
		}
	      c.is_def[i] = 1;
	    }
	  else
	    {
	      if (cur < 0)
		{
		  /* The first access is a read (a live-in value): not a
		     reload web.  */
		  ok = false;
		  break;
		}
	      open = false;
	      c.epoch_of[i] = cur;
	    }
	}
      if (!ok || cur < 0)
	continue;
      c.epochs = (unsigned) cur + 1;

      if (REGNO_REG_SET_P (df_get_live_in (bb), r))
	{
	  crp_sr_refuse (bb, "live-in", r);
	  continue;
	}
      if (REGNO_REG_SET_P (df_get_live_out (bb), r))
	{
	  crp_sr_refuse (bb, "live-out", r);
	  continue;
	}

      /* The copy half must classify identically word for word, and
	 every definition-group member must have an identical
	 pattern.  */
      ok = true;
      for (unsigned i = 0; i != n && ok; ++i)
	{
	  bool da = TEST_HARD_REG_BIT (all[i].raw_defs, r);
	  bool ua = TEST_HARD_REG_BIT (all[i].regs.uses, r);
	  bool db = TEST_HARD_REG_BIT (all[n + i].raw_defs, r);
	  bool ub = TEST_HARD_REG_BIT (all[n + i].regs.uses, r);
	  if (da != db || ua != ub)
	    {
	      crp_sr_refuse (bb, "copy-shape", r, all[n + i].insn);
	      ok = false;
	    }
	  else if (c.is_def[i])
	    {
	      /* Compare the single SET only: the SFPLOADI patterns carry a
		 scratch clobber, and two SCRATCHes never compare
		 equal.  */
	      rtx sa = single_set (all[i].insn);
	      rtx sb = single_set (all[n + i].insn);
	      if (!sa || !sb
		  || !rtx_equal_p (SET_DEST (sa), SET_DEST (sb))
		  || !rtx_equal_p (SET_SRC (sa), SET_SRC (sb)))
		{
		  crp_sr_refuse (bb, "web-mutated", r, all[n + i].insn);
		  ok = false;
		}
	    }
	}
      if (!ok)
	continue;

      /* Segments: 0 before the first group; each group opens a new
	 one.  */
      {
	unsigned seg = 0;
	bool in_group = false;
	for (unsigned i = 0; i != n; ++i)
	  {
	    if (c.is_def[i] && !in_group)
	      {
		++seg;
		in_group = true;
	      }
	    else if (!c.is_def[i])
	      in_group = false;
	    c.seg_of[i] = seg;
	  }
      }

      /* CC atoms must not take part, and no atom may span an epoch
	 boundary (the merge interleaves whole segments).  */
      ok = true;
      for (const auto &atom : lp.atoms)
	{
	  for (unsigned i = atom.first; i <= atom.second && ok; ++i)
	    if (c.is_def[i] || c.epoch_of[i] >= 0)
	      {
		crp_sr_refuse (bb, "atom-interior", r, all[i].insn);
		ok = false;
	      }
	  if (ok && c.seg_of[atom.first] != c.seg_of[atom.second])
	    {
	      crp_sr_refuse (bb, "atom-spans-epoch", r);
	      ok = false;
	    }
	  if (!ok)
	    break;
	}
      if (!ok)
	continue;

      /* The merge moves every surviving copy-half word of segment S
	 ahead of every first-half word of a later segment; each such
	 pair may interact through no register but R.  */
      ok = true;
      for (unsigned x = 0; x != n && ok; ++x)
	for (unsigned y = 0; y != n && ok; ++y)
	  {
	    if (c.seg_of[y] >= c.seg_of[x] || c.is_def[y])
	      continue;
	    HARD_REG_SET xd = all[x].raw_defs;
	    HARD_REG_SET xu = all[x].regs.uses;
	    HARD_REG_SET yd = all[n + y].raw_defs;
	    HARD_REG_SET yu = all[n + y].regs.uses;
	    CLEAR_HARD_REG_BIT (xd, r);
	    CLEAR_HARD_REG_BIT (xu, r);
	    CLEAR_HARD_REG_BIT (yd, r);
	    CLEAR_HARD_REG_BIT (yu, r);
	    if (hard_reg_set_intersect_p (xd, yu)
		|| hard_reg_set_intersect_p (xd, yd)
		|| hard_reg_set_intersect_p (xu, yd))
	      {
		crp_sr_refuse (bb, "crossrow-interference", r,
			       all[n + y].insn);
		ok = false;
	      }
	  }
      if (!ok)
	continue;

      c.removed = 0;
      for (unsigned i = 0; i != n; ++i)
	if (c.is_def[i])
	  ++c.removed;
      if (c.removed > best.removed)
	best = c;
    }

  if (best.removed == 0)
    return;

  /* The deduplicated candidate must not have a larger steady-state II
     than the duplicated one; the merge couples the halves more
     tightly, which can cost more than the removed words save.  */
  int ii_dup = crp_model_ii (all, group);
  std::vector<ls_node> merged;
  std::vector<int> merged_group;
  merged.reserve (2 * n - best.removed);
  merged_group.reserve (2 * n - best.removed);
  for (unsigned s = 0; s <= best.epochs; ++s)
    {
      for (unsigned i = 0; i != n; ++i)
	if (best.seg_of[i] == s)
	  {
	    merged.push_back (all[i]);
	    merged_group.push_back (group[i]);
	  }
      for (unsigned i = 0; i != n; ++i)
	if (best.seg_of[i] == s && !best.is_def[i])
	  {
	    merged.push_back (all[n + i]);
	    merged_group.push_back (group[n + i]);
	  }
    }
  gcc_assert (merged.size () == 2 * n - best.removed);
  for (unsigned k = 0; k != merged.size (); ++k)
    merged[k].orig = (int) k;
  int ii_dedup = crp_model_ii (merged, merged_group);
  if (ii_dedup == INT_MAX || ii_dedup > ii_dup)
    {
      crp_sr_refuse (bb, "ii-regression", best.r);
      return;
    }

  /* Fill the epoch map, then check the merged candidate before
     changing anything.  */
  crp_shared_reload_info sr;
  sr.reg = best.r;
  sr.removed = best.removed;
  for (unsigned i = 0; i != n; ++i)
    {
      if (best.is_def[i])
	sr.def_epoch.emplace_back (all[i].insn, best.seg_of[i]);
      if (best.epoch_of[i] >= 0)
	{
	  sr.consumer_epoch.emplace_back (all[i].insn, best.seg_of[i]);
	  sr.consumer_epoch.emplace_back (all[n + i].insn, best.seg_of[i]);
	}
    }
  {
    std::vector<rtx_insn *> probe = crp_candidate_order (merged,
							 merged_group);
    if (!crp_order_legal_p (merged, probe)
	|| !crp_shared_reload_order_sound_p (sr, probe))
      {
	crp_sr_refuse (bb, "final-order-unproven", best.r);
	return;
      }
  }

  /* Commit: delete the copy half's definition words and install the
     merged order.  The copies belong to this transaction, so a later
     refusal of the whole pairing still restores the original row.  */
  for (unsigned i = n; i-- != 0;)
    if (best.is_def[i])
      {
	rtx_insn *dead = all[n + i].insn;
	for (unsigned k = 0; k != copies.size (); ++k)
	  if (copies[k] == dead)
	    {
	      copies.erase (copies.begin () + k);
	      break;
	    }
	delete_insn (dead);
      }
  all.swap (merged);
  group.swap (merged_group);
  *out = sr;
  if (dump_file)
    fprintf (dump_file, "Crossrow shared-reload: reg %u epochs=%u "
	     "removed=%u II %d -> %d in bb %d\n",
	     best.r, best.epochs, best.removed, ii_dup, ii_dedup,
	     bb->index);
}

/* Preservation seeds (-mtt-tensix-optimize-crossrow-pairing-seed).

   A collision web whose root executes inside a flat CC atom cannot be
   renamed to a dead LREG directly: the predicated root writes only the
   enabled lanes, so the dead register's stale disabled-lane bits would
   reach an all-lanes consumer.  It can be renamed if an all-lanes copy
   F = R (SFPMOV mod 2, rvtt_sfpassign in rvtt.md) is inserted right after
   the last definition of R that precedes the root: before the atom's
   first CC writer if R is not written in the atom before the root, or
   inside the atom right after R's last definition there.  SFPMOV mod 2
   writes every lane regardless of CC, and the seed joins the atom's
   item so the original words keep their order and CC contexts.  Then:

     - at the seed, F receives R's value on all 32 lanes;
     - between the seed and the root neither R nor F changes, so F == R
       on every lane at the root;
     - the root writes the same enabled lanes it wrote into R, and F's
       disabled lanes hold what R's held, which also covers a
       read-modify-write root;
     - every later member of the web is rewritten to F up to the web's
       end, so an all-lanes store sees the same value.

   Each seed is an issued word and is charged by the II model and the
   capture budget like any other.  Seeds are kept only on a strict II
   improvement over the unseeded candidate; a seed that does not change
   the II is accepted only if a later seed then improves it, and any
   trailing seeds that do not improve the II are rolled back.  */

/* Report a seed-phase refusal.  */

static void
crp_seed_refuse (basic_block bb, const char *why, unsigned r, rtx_insn *insn)
{
  rvtt_refusal_fire_composed ("crossrow-pairing-seed", why);
  if (dump_file)
    fprintf (dump_file, "Crossrow pairing seed refused: "
	     "crossrow-pairing-seed-%s reg %u uid=%d in bb %d\n",
	     why, r, insn ? INSN_UID (insn) : -1, bb->index);
}

/* Rename the web rooted at node I from register R to F (nodes I through
   EXTENT_END exclusive that reference R, including read-modify-write
   redefinitions) as one recorded transaction, as
   ls_cyclic_rename_collisions does.  */

static bool
crp_apply_web_rename (std::vector<ls_node> &nodes, unsigned i,
		      unsigned extent_end, unsigned r, unsigned f,
		      std::vector<ls_rename> *record)
{
  ls_rename rn;
  rn.oldr = r;
  rn.newr = f;
  ls_queue_reg_replacements (nodes[i].insn, &PATTERN (nodes[i].insn), r, f);
  rn.insns.push_back (nodes[i].insn);
  for (unsigned k = i + 1; k != extent_end; ++k)
    if (TEST_HARD_REG_BIT (nodes[k].regs.uses, r)
	|| TEST_HARD_REG_BIT (nodes[k].raw_defs, r))
      {
	ls_queue_reg_replacements (nodes[k].insn, &PATTERN (nodes[k].insn),
				   r, f);
	rn.insns.push_back (nodes[k].insn);
      }
  if (!apply_change_group ())
    return false;
  for (rtx_insn *ins : rn.insns)
    df_insn_rescan (ins);
  record->push_back (std::move (rn));
  return true;
}

/* Undo the last recorded rename (used to roll back one seed
   candidate).  */

static void
crp_undo_last_rename (std::vector<ls_rename> *record)
{
  gcc_assert (!record->empty ());
  ls_rename &rn = record->back ();
  for (rtx_insn *ins : rn.insns)
    ls_queue_reg_replacements (ins, &PATTERN (ins), rn.newr, rn.oldr);
  bool ok = apply_change_group ();
  gcc_assert (ok);
  for (rtx_insn *ins : rn.insns)
    df_insn_rescan (ins);
  record->pop_back ();
}

/* Modulo variable expansion (-mtt-tensix-optimize-mve-expand).

   This performs the kernel unroll that the IMS path only accounts for.
   On a loop accepted by the cross-row pairing (so the result stays
   replay-formable), the doubled row is ordered by an item-level modulo
   placement of the single kernel (CC atoms as indivisible items, as in
   the pairing's greedy scheduler) with the second iteration offset by
   one placement II (Lam, PLDI 1988, with kmin = 2 as the pairing's two
   copies), instead of the greedy interleave.  The copy's colliding webs
   are rotated through rvtt_lreg_rename_chain, the register demand is
   checked against the register file net of loop-live invariants as in
   the IMS path, and a producer-lockstep check verifies that every
   consumer still reads the same producer after the renames.  The result
   is kept only if it passes the pairing's acceptance test and strictly
   beats the greedy pairing candidate; otherwise the renames are undone.

   Refusals:
     mve-expand-row-mutated            seeds or the shared-reload dedupe
					changed the textual pairing
     mve-expand-kmin-beyond-pairing    lifetimes need more than 2 copies
     mve-rename-exhausted              the copies do not fit the file
					net of loop-live invariants
     mve-expand-lockstep-divergence    a renamed web changed which
					producer a consumer reads
     mve-expand-order-hazard           an unrenamed shared web would
					reverse an original-order
					dependence
     mve-expand-no-ii-decrease         the order does not strictly beat
					the greedy candidate and the
					baseline  */

/* Return, for each node of ALL in index order, the sorted multiset of
   the nodes producing its register uses (-1 for live into the row).  A
   value-preserving whole-web rename leaves this unchanged; a mis-rotated
   or partially renamed web changes it.  */

static std::vector<std::vector<int> >
mve_producer_map (const std::vector<ls_node> &all)
{
  std::vector<std::vector<int> > map (all.size ());
  for (unsigned v = 0; v != all.size (); ++v)
    {
      for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
	{
	  if (!TEST_HARD_REG_BIT (all[v].regs.uses, r))
	    continue;
	  int producer = -1;
	  for (unsigned j = v; j-- != 0;)
	    if (TEST_HARD_REG_BIT (all[j].raw_defs, r))
	      {
		producer = (int) j;
		break;
	      }
	  map[v].push_back (producer);
	}
      for (unsigned i = 1; i < map[v].size (); ++i)  /* insertion sort */
	{
	  int x = map[v][i];
	  unsigned j = i;
	  while (j > 0 && map[v][j - 1] > x)
	    {
	      map[v][j] = map[v][j - 1];
	      --j;
	    }
	  map[v][j] = x;
	}
    }
  return map;
}

/* Perform the expansion.  ALL/GROUP hold the doubled row after the
   collision renames; N is the single-kernel node count and BASE_II the
   doubled sequential baseline.  On success return true with
   *CANDIDATE_OUT the order, *II_OUT its modeled steady-state II (below
   both the greedy candidate's and BASE_II), *PLACE_II_OUT the kernel
   placement II, and *WEBS_OUT the committed renames (which the caller
   undoes on a later refusal).  On refusal return false with the state
   restored.  */

static bool
crp_mve_expand_arm (basic_block bb, std::vector<ls_node> &all,
		    std::vector<int> &group, unsigned n, int base_ii,
		    bool row_mutated,
		    std::vector<rtx_insn *> *candidate_out, int *ii_out,
		    int *place_ii_out,
		    std::vector<rvtt_lreg_rename_web> *webs_out)
{
  if (row_mutated || all.size () != 2 * n)
    {
      rvtt_refuse (RVTT_REF_MVE_EXPAND_ROW_MUTATED, dump_file,
		   "Crossrow mve-expand refused: mve-expand-row-mutated "
		   "in bb %d\n", bb->index);
      return false;
    }
  /* The two halves must have the same item structure.  */
  for (unsigned k = 0; k != n; ++k)
    {
      bool a_open = group[k] >= 0 && k > 0 && group[k - 1] == group[k];
      bool b_open = group[n + k] >= 0 && group[n + k - 1] == group[n + k];
      if ((group[k] >= 0) != (group[n + k] >= 0)
	  || (k > 0 && a_open != b_open))
	{
	  rvtt_refuse (RVTT_REF_MVE_EXPAND_ROW_MUTATED, dump_file,
		       "Crossrow mve-expand refused: mve-expand-row-mutated "
		       "(halves not item-isomorphic) in bb %d\n", bb->index);
	  return false;
	}
    }

  /* Items over the single kernel (first half): each atom instance is
     one multi-word item, each pure word its own item.  */
  struct mve_item
  {
    std::vector<unsigned> members;	/* indices into ALL, first half */
    HARD_REG_SET uses, raw_defs;
    int words, lat;
  };
  std::vector<mve_item> items;
  {
    unsigned i = 0;
    while (i != n)
      {
	mve_item it;
	CLEAR_HARD_REG_SET (it.uses);
	CLEAR_HARD_REG_SET (it.raw_defs);
	it.words = 0;
	it.lat = 0;
	unsigned end = i + 1;
	if (group[i] >= 0)
	  while (end != n && group[end] == group[i])
	    ++end;
	for (unsigned k = i; k != end; ++k)
	  {
	    it.members.push_back (k);
	    it.uses |= all[k].regs.uses;
	    it.raw_defs |= all[k].raw_defs;
	    it.words += all[k].words;
	    if (all[k].lat > it.lat)
	      it.lat = all[k].lat;
	  }
	items.push_back (std::move (it));
	i = end;
      }
  }
  const unsigned m = items.size ();

  /* Item-level dependences for the engine, with each item's latency
     the maximum of its members (overestimating only widens the
     placement).  The intra-iteration matrix uses the full register
     classification.  The cross-iteration matrix only keeps
     interactions through registers live into the row (invariants and
     loop-carried values), since every other register is renamed by the
     rotation.  This is optimistic, but only for generating the
     candidate: the acceptance model, the legality check and the
     producer-lockstep check judge the final order.  */
  HARD_REG_SET nonrot;
  CLEAR_HARD_REG_SET (nonrot);
  for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
    if (REGNO_REG_SET_P (df_get_live_in (bb), r))
      SET_HARD_REG_BIT (nonrot, r);
  rvtt_timing::seq s, cross;
  s.ops.resize (m);
  s.dep.resize (m * m);
  cross.dep.resize (m * m);
  for (unsigned a = 0; a != m; ++a)
    {
      s.ops[a].words = items[a].words;
      s.ops[a].lat = items[a].lat;
      s.ops[a].entry_pin = 0;
      HARD_REG_SET da = items[a].raw_defs;
      HARD_REG_SET ua = items[a].uses;
      da &= nonrot;
      ua &= nonrot;
      for (unsigned b = 0; b != m; ++b)
	{
	  s.dep[a * m + b] = (unsigned char) rvtt_timing::classify_dependence
	    (hard_reg_set_intersect_p (items[a].raw_defs, items[b].uses)
	     || hard_reg_set_intersect_p (items[a].raw_defs,
					  items[b].raw_defs),
	     hard_reg_set_intersect_p (items[a].uses, items[b].raw_defs));
	  cross.dep[a * m + b]
	    = (unsigned char) rvtt_timing::classify_dependence
	      (hard_reg_set_intersect_p (da, items[b].uses)
	       || hard_reg_set_intersect_p (da, items[b].raw_defs),
	       hard_reg_set_intersect_p (ua, items[b].raw_defs));
	}
    }
  cross.ops = s.ops;
  rvtt_timing::mod_prob prob = rvtt_timing::make_mod_prob (s, cross);

  /* Value lifetimes for the expansion count only RAW flow.  The
     placement above must keep the RAW/WAW constraints, but kmin and the
     live-copy demand count value copies (Lam's definition); a WAW-only
     span is removed by renaming.  */
  rvtt_timing::seq raws, rawc;
  raws.ops = s.ops;
  rawc.ops = s.ops;
  raws.dep.assign (m * m, (unsigned char) rvtt_timing::DEP_NONE);
  rawc.dep.assign (m * m, (unsigned char) rvtt_timing::DEP_NONE);
  {
    /* Use nearest-definition flow rather than register overlap:
       with register reuse, a late reader of a reused register does not
       read the earlier value.  */
    std::vector<unsigned> item_of (n, 0);
    for (unsigned a = 0; a != m; ++a)
      for (unsigned k : items[a].members)
	item_of[k] = a;
    for (unsigned v = 0; v != n; ++v)
      for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
	{
	  if (!TEST_HARD_REG_BIT (all[v].regs.uses, r))
	    continue;
	  int producer = -1;
	  for (unsigned j = v; j-- != 0;)
	    if (TEST_HARD_REG_BIT (all[j].raw_defs, r))
	      {
		producer = (int) j;
		break;
	      }
	  if (producer >= 0)
	    raws.dep[item_of[producer] * m + item_of[v]]
	      = (unsigned char) rvtt_timing::DEP_LATENCY;
	  else
	    /* Upward-exposed use: loop-carried if the row redefines the
	       register later (distance-1 flow), otherwise an invariant.
	       Either way the register is in NONROT.  */
	    for (unsigned j = v + 1; j != n; ++j)
	      if (TEST_HARD_REG_BIT (all[j].raw_defs, r))
		{
		  rawc.dep[item_of[j] * m + item_of[v]]
		    = (unsigned char) rvtt_timing::DEP_LATENCY;
		  break;
		}
	}
  }
  rvtt_timing::mod_prob prob_raw = rvtt_timing::make_mod_prob (raws, rawc);
  int res = rvtt_timing::resmii (prob);
  int rec = rvtt_timing::recmii (prob);
  int mii = res > rec ? res : rec;
  if (dump_file)
    {
      unsigned omega1 = 0;
      for (unsigned k = 0; k != prob.edges.size (); ++k)
	omega1 += prob.edges[k].omega;
      fprintf (dump_file, "Crossrow mve-expand model: bb %d items=%u "
	       "ResMII=%d RecMII=%d cross-edges=%u\n",
	       bb->index, m, res, rec, omega1);
    }

  /* The greedy pairing candidate that this path must beat.  The
     placement bound is the baseline only: the greedy candidate is
     priced before rotation, and the rotation renames can bring the II
     below what the unrotated registers allow.  */
  int crp_ii = crp_model_ii (all, group);
  int bound = (base_ii - 1) / 2;	/* realized II ~ 2 * place-II */
  if (rec < 0 || mii > bound)
    {
      rvtt_refuse (RVTT_REF_MVE_EXPAND_NO_II_DECREASE, dump_file,
		   "Crossrow mve-expand refused: mve-expand-no-ii-decrease "
		   "in bb %d (MII %d, place bound %d)\n",
		   bb->index, mii, bound);
      return false;
    }
  /* Check the expansion demand as the IMS path does: peak live value
     copies against the file net of loop-live invariants (live into the
     row, defined by no kernel node).  */
  unsigned invariants = 0;
  for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
    {
      if (!REGNO_REG_SET_P (df_get_live_in (bb), r))
	continue;
      bool defined = false;
      for (unsigned i = 0; i != n && !defined; ++i)
	defined = TEST_HARD_REG_BIT (all[i].raw_defs, r);
      if (!defined)
	++invariants;
    }
  unsigned capacity = rvtt_pressure_capacity ();
  unsigned net = capacity > invariants ? capacity - invariants : 0;

  /* Lam's smallest-fitting-II rule: place at each II from MII up to the
     bound and take the first placement whose expansion fits.  A higher
     II shortens the overlap, so the demand only falls.  A refusal
     reports the reason found at the lowest placed II.  */
  int budget = riscv_tt_ims_budget > 0 ? (int) riscv_tt_ims_budget
					: 8 * (int) m;
  rvtt_timing::mod_placement pl;
  int kmin = 0;
  unsigned demand = 0;
  bool any_budget_exhausted = false, fits = false;
  int first_kmin = 0;
  unsigned first_demand = 0;
  int first_ii = 0;
  for (int ii = mii; ii <= bound && !fits; ++ii)
    {
      rvtt_timing::mod_placement at
	= rvtt_timing::ims_schedule (prob, ii, ii, budget);
      if (!at.scheduled)
	{
	  any_budget_exhausted |= at.budget_exhausted;
	  continue;
	}
      int k = rvtt_timing::mve_kmin (prob_raw, at);
      unsigned d = rvtt_timing::mve_live_demand (prob_raw, at);
      if (k == 1)
	{
	  /* No lifetime exceeds this II, so nothing needs expanding.  If no
	     lower II needed an expansion, the row needs none (a dump note,
	     not a refusal); otherwise fall through to report the lower
	     II's demand.  */
	  if (!first_ii && dump_file)
	    fprintf (dump_file, "Crossrow mve-expand: bb %d kmin=1 at "
		     "place-II=%d -- no expansion owed, greedy candidate "
		     "proceeds\n", bb->index, at.ii);
	  if (!first_ii)
	    return false;
	  break;
	}
      if (!first_ii)
	{
	  first_ii = at.ii;
	  first_kmin = k;
	  first_demand = d;
	}
      if (k > 2 || d > net)
	continue;
      pl = at;
      kmin = k;
      demand = d;
      fits = true;
    }
  if (!fits)
    {
      if (first_ii && first_kmin > 2)
	rvtt_refuse (RVTT_REF_MVE_EXPAND_KMIN_BEYOND_PAIRING, dump_file,
		     "Crossrow mve-expand refused: "
		     "mve-expand-kmin-beyond-pairing in bb %d (kmin=%d at "
		     "place-II=%d)\n", bb->index, first_kmin, first_ii);
      else if (first_ii)
	rvtt_refuse (RVTT_REF_MVE_RENAME_EXHAUSTED, dump_file,
		     "Crossrow mve-expand refused: mve-rename-exhausted "
		     "in bb %d (kmin=%d demand=%u capacity=%u invariants=%u "
		     "at place-II=%d)\n",
		     bb->index, first_kmin, first_demand, capacity,
		     invariants, first_ii);
      else if (any_budget_exhausted)
	rvtt_refuse (RVTT_REF_IMS_BUDGET_EXHAUSTED, dump_file,
		     "Crossrow mve-expand refused: ims-budget-exhausted "
		     "in bb %d (MII %d, bound %d, budget %d)\n",
		     bb->index, mii, bound, budget);
      else
	rvtt_refuse (RVTT_REF_MVE_EXPAND_NO_II_DECREASE, dump_file,
		     "Crossrow mve-expand refused: mve-expand-no-ii-decrease "
		     "in bb %d (no feasible placement below %d)\n",
		     bb->index, bound);
      return false;
    }

  /* Producer-lockstep reference, taken before any rotation.  */
  std::vector<std::vector<int> > producers_before = mve_producer_map (all);

  /* Rotation renames: every copy-half definition whose register the
     first iteration also defines is a colliding web that must be
     renamed.  rvtt_lreg_rename_chain chooses targets (free in the block
     first, then its temporal tier) and checks legality.  One attempt
     per root insn; committed webs are recorded for undo.  */
  std::vector<rvtt_lreg_rename_web> webs;
  auto undo_webs = [&] ()
    {
      for (unsigned i = webs.size (); i--;)
	rvtt_lreg_rename_web_undo (webs[i]);
      if (!webs.empty () && dump_file)
	fprintf (dump_file, "Crossrow mve-expand: undid %zu rotation "
		 "rename(s) in bb %d\n", webs.size (), bb->index);
      webs.clear ();
      ls_refresh_node_regs (all);
    };
  /* Choose rotation targets by placement-slot arithmetic (Lam's
     rotation, kmin = 2): a register's occupancy in the steady state is
     the union of its webs' placement-slot windows (copy B offset by one
     placement II, repeating every 2*II), and a copy-B web moves to a
     register whose windows are free over the web's own window modulo
     2*II.  This only chooses the target; rvtt_lreg_rename_chain checks
     the rename in sequential order, and the acceptance model judges
     whatever could not be rotated.  */
  const long period = 2 * (long) pl.ii;
  std::vector<unsigned> item_of_node (n, 0);
  for (unsigned a = 0; a != m; ++a)
    for (unsigned k : items[a].members)
      item_of_node[k] = a;
  auto node_slot = [&] (unsigned v) -> long	/* v indexes ALL */
    {
      unsigned half = v < n ? 0 : 1;
      return pl.sigma[item_of_node[v - half * n]] + (long) half * pl.ii;
    };
  /* Occupancy windows per register over the current doubled row.  */
  struct mve_win { long lo, hi; };
  std::vector<std::vector<mve_win> > occ (SFPU_REG_LAST + 1);
  auto add_windows = [&] ()
    {
      for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
	{
	  occ[r].clear ();
	  for (unsigned half = 0; half != 2; ++half)
	    {
	      bool any = false;
	      long lo = 0, hi = 0;
	      for (unsigned k = 0; k != n; ++k)
		{
		  unsigned v = half * n + k;
		  if (!TEST_HARD_REG_BIT (all[v].regs.uses, r)
		      && !TEST_HARD_REG_BIT (all[v].raw_defs, r))
		    continue;
		  long s = node_slot (v);
		  long e = s + all[v].words + all[v].lat;
		  if (!any)
		    {
		      lo = s;
		      hi = e;
		      any = true;
		    }
		  else
		    {
		      if (s < lo)
			lo = s;
		      if (e > hi)
			hi = e;
		    }
		}
	      if (any)
		occ[r].push_back ({lo, hi});
	    }
	}
    };
  auto wins_conflict = [&] (const mve_win &a, const mve_win &b) -> bool
    {
      for (int k = -1; k != 2; ++k)
	{
	  long b1 = b.lo + k * period, b2 = b.hi + k * period;
	  if (a.lo <= b2 && b1 <= a.hi)
	    return true;
	}
      return false;
    };
  for (unsigned k = 0; k != n; ++k)
    for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
      {
	if (!TEST_HARD_REG_BIT (all[n + k].raw_defs, r)
	    || TEST_HARD_REG_BIT (all[n + k].regs.uses, r))
	  continue;		/* not a fresh-definition root */
	bool collision = false;
	for (unsigned j = 0; j != n && !collision; ++j)
	  collision = TEST_HARD_REG_BIT (all[j].raw_defs, r);
	if (!collision)
	  continue;
	/* The copy web's window: the definition through the last
	   copy-half access before the next fresh redefinition.  */
	mve_win w;
	w.lo = node_slot (n + k);
	w.hi = w.lo + all[n + k].words + all[n + k].lat;
	for (unsigned j = k + 1; j != n; ++j)
	  {
	    unsigned v = n + j;
	    if (TEST_HARD_REG_BIT (all[v].raw_defs, r)
		&& !TEST_HARD_REG_BIT (all[v].regs.uses, r))
	      break;		/* next fresh writer: web ends */
	    if (TEST_HARD_REG_BIT (all[v].regs.uses, r)
		|| TEST_HARD_REG_BIT (all[v].raw_defs, r))
	      {
		long e = node_slot (v) + all[v].words + all[v].lat;
		if (e > w.hi)
		  w.hi = e;
	      }
	  }
	add_windows ();
	for (unsigned t = SFPU_REG_FIRST; t <= SFPU_REG_LAST; ++t)
	  {
	    if (t == r || fixed_regs[t] || TEST_HARD_REG_BIT (nonrot, t))
	      continue;
	    bool free_across = true;
	    for (const mve_win &o : occ[t])
	      if (wins_conflict (w, o))
		{
		  free_across = false;
		  break;
		}
	    if (!free_across)
	      continue;
	    rvtt_lreg_rename_web web;
	    if (!rvtt_lreg_rename_chain (bb, all[n + k].insn,
					 (int) (t - SFPU_REG_FIRST), &web,
					 RVTT_RENAME_SHAPE_OWNED))
	      continue;		/* refused by the rename service; try
				   the next free target */
	    webs.push_back (web);
	    ls_refresh_node_regs (all);
	    if (dump_file)
	      fprintf (dump_file, "Crossrow mve-expand: rotation rename "
		       "L%d -> L%d at uid=%d in bb %d (window %ld..%ld "
		       "mod %ld)\n",
		       web.old_l, web.new_l, INSN_UID (all[n + k].insn),
		       bb->index, w.lo, w.hi, period);
	    break;
	  }
	break;			/* one rename attempt per root insn */
      }

  /* Testing knob: point the last committed web's writer back at its
     old register while its readers keep the new one, a partial-web
     error that the check below must catch.  */
  bool fault_injected = false;
  rtx_insn *fault_insn = nullptr;
  unsigned fault_oldr = 0, fault_newr = 0;
  if (riscv_tt_mve_expand_inject_fault && !webs.empty ())
    {
      const rvtt_lreg_rename_web &w = webs.back ();
      fault_insn = w.insns[0];
      fault_oldr = SFPU_REG_FIRST + w.new_l;
      fault_newr = SFPU_REG_FIRST + w.old_l;
      ls_queue_reg_replacements (fault_insn, &PATTERN (fault_insn), fault_oldr,
				 fault_newr);
      if (apply_change_group ())
	{
	  df_insn_rescan (fault_insn);
	  ls_refresh_node_regs (all);
	  fault_injected = true;
	}
    }
  auto undo_injected_fault = [&] ()
    {
      if (!fault_injected)
	return;
      ls_queue_reg_replacements (fault_insn, &PATTERN (fault_insn), fault_newr,
				 fault_oldr);
      bool ok = apply_change_group ();
      gcc_assert (ok);
      df_insn_rescan (fault_insn);
      ls_refresh_node_regs (all);
      fault_injected = false;
    };

  /* Producer-lockstep check: every consumer's producer multiset must
     be unchanged.  This is recomputed from the register sets,
     independently of the dependence model.  */
  std::vector<std::vector<int> > producers_after = mve_producer_map (all);
  if (producers_after != producers_before)
    {
      gcc_checking_assert (fault_injected);	/* the service cannot diverge */
      undo_injected_fault ();
      undo_webs ();
      rvtt_refuse (RVTT_REF_MVE_EXPAND_LOCKSTEP_DIVERGENCE, dump_file,
		   "Crossrow mve-expand refused: "
		   "mve-expand-lockstep-divergence in bb %d (renames "
		   "undone)\n", bb->index);
      return false;
    }
  /* If the check passed under the testing knob, the injected edit never
     applied; drop it either way before judging the order.  */
  undo_injected_fault ();

  /* Emit both halves' items at their modulo issue slots, the copy
     offset by one placement II; atom members stay contiguous and in
     order.  The emission is a slot-keyed topological walk over the item
     dependences recomputed after the renames: an item issues at its
     placement slot unless a remaining dependence (an unrenamed shared
     web) holds it back, in which case it waits for its predecessors.
     The acceptance model below accounts for any such delay.  */
  struct mve_ditem
  {
    HARD_REG_SET uses, defs;
    long slot;
    unsigned half, idx;
  };
  std::vector<mve_ditem> ditems (2 * m);
  for (unsigned half = 0; half != 2; ++half)
    for (unsigned a = 0; a != m; ++a)
      {
	mve_ditem &d = ditems[half * m + a];
	d.half = half;
	d.idx = a;
	CLEAR_HARD_REG_SET (d.uses);
	CLEAR_HARD_REG_SET (d.defs);
	for (unsigned k : items[a].members)
	  {
	    d.uses |= all[half * n + k].regs.uses;
	    d.defs |= all[half * n + k].raw_defs;
	  }
	d.slot = pl.sigma[a] + (long) half * pl.ii;
      }
  auto ditem_dep = [&] (unsigned j, unsigned i) -> bool
    {
      /* Original-order dependence j -> i (j earlier).  */
      return hard_reg_set_intersect_p (ditems[j].defs, ditems[i].uses)
	     || hard_reg_set_intersect_p (ditems[j].defs, ditems[i].defs)
	     || hard_reg_set_intersect_p (ditems[j].uses, ditems[i].defs);
    };
  candidate_out->clear ();
  std::vector<bool> emitted (2 * m, false);
  for (unsigned step = 0; step != 2 * m; ++step)
    {
      int best = -1;
      for (unsigned i = 0; i != 2 * m; ++i)
	{
	  if (emitted[i])
	    continue;
	  bool ready = true;
	  for (unsigned j = 0; j != i && ready; ++j)
	    if (!emitted[j] && ditem_dep (j, i))
	      ready = false;
	  if (!ready)
	    continue;
	  if (best < 0
	      || ditems[i].slot < ditems[best].slot
	      || (ditems[i].slot == ditems[best].slot
		  && (ditems[i].half < ditems[best].half
		      || (ditems[i].half == ditems[best].half
			  && ditems[i].idx < ditems[best].idx))))
	    best = (int) i;
	}
      gcc_assert (best >= 0);	/* original order is always admissible */
      emitted[best] = true;
      for (unsigned k : items[ditems[best].idx].members)
	candidate_out->push_back (all[ditems[best].half * n + k].insn);
    }

  /* Original-order dependences that remain after the renames must keep
     their direction.  */
  if (!crp_order_legal_p (all, *candidate_out))
    {
      undo_webs ();
      rvtt_refuse (RVTT_REF_MVE_EXPAND_ORDER_HAZARD, dump_file,
		   "Crossrow mve-expand refused: mve-expand-order-hazard "
		   "in bb %d (renames undone)\n", bb->index);
      return false;
    }

  /* The steady state of the result, judged by the wrapped acceptance
     model, must strictly beat both the greedy candidate and the doubled
     sequential baseline.  */
  std::vector<int> idx;
  for (rtx_insn *ci : *candidate_out)
    for (unsigned i = 0; i != all.size (); ++i)
      if (all[i].insn == ci)
	{
	  idx.push_back ((int) i);
	  break;
	}
  int mve_ii = idx.size () == all.size () ? ls_cyclic_ii (all, idx)
					   : INT_MAX;
  if (mve_ii >= (crp_ii < base_ii ? crp_ii : base_ii))
    {
      undo_webs ();
      rvtt_refuse (RVTT_REF_MVE_EXPAND_NO_II_DECREASE, dump_file,
		   "Crossrow mve-expand refused: mve-expand-no-ii-decrease "
		   "in bb %d (realized %d vs greedy %d, base %d)\n",
		   bb->index, mve_ii, crp_ii, base_ii);
      return false;
    }

  if (dump_file)
    fprintf (dump_file, "Crossrow mve-expand: bb %d items=%u "
	     "ResMII=%d RecMII=%d place-II=%d kmin=%d demand=%u "
	     "invariants=%u renames=%zu realized II %d (greedy %d, "
	     "base %d)\n",
	     bb->index, m, res, rec, pl.ii, kmin, demand, invariants,
	     webs.size (), mve_ii, crp_ii, base_ii);
  *ii_out = mve_ii;
  *place_ii_out = pl.ii;
  *webs_out = webs;
  return true;
}

/* Perform the cross-row pairing on BB.  Return true if it was
   committed.  */

static bool
crp_pair_loop (basic_block bb, std::vector<basic_block> &visited)
{
  crp_loop lp;
  if (!crp_admit_loop (bb, &lp))
    return false;

  unsigned n = lp.nodes.size ();

  /* Emit row B as a copy of row A, after A's last word and before the
     separator.  Any later refusal deletes the copies; A's own words are
     not modified before the commit point.  */
  std::vector<rtx_insn *> copies;
  rtx_insn *after = lp.nodes[n - 1].insn;
  for (unsigned i = 0; i != n; ++i)
    {
      rtx_insn *cp = emit_insn_after (copy_insn (PATTERN (lp.nodes[i].insn)),
				      after);
      df_insn_rescan (cp);
      copies.push_back (cp);
      after = cp;
    }
  auto crp_delete_copies = [&copies] ()
    {
      for (rtx_insn *cp : copies)
	delete_insn (cp);
    };

  /* Rebase row B's Dst accesses to the second row of the shared
     counter frame.  */
  crp_queue_dst_rebase (copies[lp.load], lp.dst_addr + lp.dst_step);
  crp_queue_dst_rebase (copies[lp.store], lp.dst_addr + lp.dst_step);
  if (!apply_change_group ())
    {
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-dst-rebase-constraint");
    }
  df_insn_rescan (copies[lp.load]);
  df_insn_rescan (copies[lp.store]);

  /* Build the node vectors over the doubled row.  The copies go
     through the same admission; a failure here is a refusal.  */
  std::vector<ls_node> all = lp.nodes;
  for (unsigned i = 0; i != n; ++i)
    {
      ls_node node;
      if (!crp_node (bb, copies[i], &node))
	{
	  crp_delete_copies ();
	  return crp_refuse (bb, "crossrow-pairing-copy-unproven",
			     copies[i]);
	}
      node.orig = n + i;
      all.push_back (node);
    }

  /* Baseline: the two iterations in their original sequential order,
     judged by the same cyclic steady-state model as the candidate and
     measured before any rename.  */
  std::vector<int> base_order (2 * n);
  for (unsigned i = 0; i != 2 * n; ++i)
    base_order[i] = i;
  int base_ii = ls_cyclic_ii (all, base_order);
  unsigned pads_before = ls_pad_sites (visited, bb, all);

  /* Break false recurrences introduced by the allocator; webs may only
     be rooted where all lanes are enabled (never inside an atom).  */
  std::vector<bool> start_allowed (2 * n, true);
  for (unsigned half = 0; half != 2; ++half)
    for (const auto &atom : lp.atoms)
      for (unsigned i = atom.first; i <= atom.second; ++i)
	start_allowed[half * n + i] = false;
  std::vector<ls_rename> renames;
  /* With the stall-words option, the copy half's webs get the free
     LREGs first: breaking row B's serialization is the point of the
     pairing.  For example, with three free LREGs and three row-B webs,
     a row-A WAW web taking one would leave the row-B accumulator
     serialized and the II check would fail.  */
  std::vector<unsigned> scan_order;
  if (riscv_tt_opt_crossrow_pairing_stall_words > 0)
    {
      for (unsigned i = n; i != 2 * n; ++i)
	scan_order.push_back (i);
      for (unsigned i = 0; i != n; ++i)
	scan_order.push_back (i);
    }
  ls_cyclic_rename_collisions (bb, all, &renames, &start_allowed,
			       scan_order.empty () ? nullptr : &scan_order);

  /* Item grouping over the doubled row: each atom instance is one
     item id; pure words (and later preservation seeds) are -1.  */
  std::vector<int> group (2 * n, -1);
  {
    int gid = 0;
    for (unsigned half = 0; half != 2; ++half)
      for (const auto &atom : lp.atoms)
	{
	  for (unsigned i = atom.first; i <= atom.second; ++i)
	    group[half * n + i] = gid;
	  ++gid;
	}
  }

  /* Modeled steady-state II of the current candidate order (INT_MAX
     if no order can be built).  */
  auto crp_current_ii = [&all, &group] () -> int
    {
      std::vector<rtx_insn *> cand = crp_candidate_order (all, group);
      if (!crp_order_legal_p (all, cand))
	return INT_MAX;
      std::vector<int> idx;
      for (rtx_insn *ci : cand)
	for (unsigned k = 0; k != all.size (); ++k)
	  if (all[k].insn == ci)
	    {
	      idx.push_back ((int) k);
	      break;
	    }
      if (idx.size () != all.size ())
	return INT_MAX;
      return ls_cyclic_ii (all, idx);
    };

  /* Preservation seeds (see the comment before crp_seed_refuse).  Any
     refusal keeps the unseeded state.  */
  std::vector<rtx_insn *> seed_insns;
  auto crp_delete_seeds = [&seed_insns] ()
    {
      for (unsigned k = seed_insns.size (); k--;)
	delete_insn (seed_insns[k]);
      seed_insns.clear ();
    };
  if (riscv_tt_opt_crossrow_pairing_seed)
    {
      int rule_a_ii = crp_current_ii ();
      int cur_ii = rule_a_ii;
      int strict_ii = rule_a_ii;	/* best strictly-improved II */
      unsigned strict_commits = 0;	/* commits kept at that point */
      /* One entry per accepted seeded rename, parallel to the tail of
	 RENAMES: the emitted seed word, or null for a full-lane root,
	 which writes every lane itself and needs no seed.  */
      std::vector<rtx_insn *> commits;
      bool progress = rule_a_ii != INT_MAX;
      while (progress)
	{
	  progress = false;
	  for (unsigned i = 0; i != all.size () && !progress; ++i)
	    {
	      if (group[i] < 0)
		continue;	/* seeded roots live inside atoms */
	      unsigned af = i;
	      while (af && group[af - 1] == group[i])
		--af;
	      if (af == i)
		continue;	/* the atom-opening CC writer cannot root
				   a seeded web: there is no point between
				   it and the seed */
	      for (unsigned r = SFPU_REG_FIRST;
		   r <= SFPU_REG_LAST && !progress; ++r)
		{
		  if (!TEST_HARD_REG_BIT (all[i].raw_defs, r))
		    continue;
		  bool earlier = false;
		  for (unsigned j = 0; j != i && !earlier; ++j)
		    earlier = TEST_HARD_REG_BIT (all[j].raw_defs, r);
		  if (!earlier)
		    continue;	/* no collision */
		  /* A full-lane root needs no seed: the bare all-lanes copy
		     (SFPMOV mod 2) writes every lane regardless of CC, so
		     the new register holds the complete value from the
		     root on.  */
		  bool full_lane_root = bare_lreg_copy_p (all[i].insn)
		    && !TEST_HARD_REG_BIT (all[i].regs.uses, r);
		  /* Seed placement for predicated roots: the seed goes
		     after the last definition of R that precedes the
		     root, either just before the atom if R is not written
		     in the atom before the root, or inside the atom right
		     after R's last definition there (for example the
		     atom-opening compare).  The seed writes every lane
		     regardless of CC and joins the atom's item, so the
		     original words keep their order and CC contexts.  */
		  unsigned seed_pos = af;
		  for (unsigned k = af; k != i; ++k)
		    if (TEST_HARD_REG_BIT (all[k].raw_defs, r))
		      seed_pos = k + 1;
		  int seed_group = seed_pos == af ? -1 : group[i];
		  if (REGNO_REG_SET_P (df_get_live_in (bb), r))
		    {
		      crp_seed_refuse (bb, "live-in", r, all[i].insn);
		      continue;
		    }
		  /* Web extent: through read-modify-write redefinitions, up
		     to the next fresh writer.  */
		  unsigned extent_end = all.size ();
		  bool fresh_terminator = false;
		  for (unsigned k = i + 1; k != all.size (); ++k)
		    if (TEST_HARD_REG_BIT (all[k].raw_defs, r)
			&& !TEST_HARD_REG_BIT (all[k].regs.uses, r))
		      {
			extent_end = k;
			fresh_terminator = true;
			break;
		      }
		  if (!fresh_terminator
		      && REGNO_REG_SET_P (df_get_live_out (bb), r))
		    {
		      crp_seed_refuse (bb, "live-out", r, all[i].insn);
		      continue;
		    }
		  int f = -1;
		  for (unsigned c = SFPU_REG_FIRST;
		       c <= SFPU_REG_LAST && f < 0; ++c)
		    {
		      if (fixed_regs[c])
			continue;
		      bool touched = false;
		      for (unsigned j = 0; j != all.size () && !touched; ++j)
			touched = TEST_HARD_REG_BIT (all[j].regs.uses, c)
				  || TEST_HARD_REG_BIT (all[j].raw_defs, c);
		      if (touched
			  || REGNO_REG_SET_P (df_get_live_in (bb), c)
			  || REGNO_REG_SET_P (df_get_live_out (bb), c))
			continue;
		      f = (int) c;
		    }
		  if (f < 0)
		    {
		      crp_seed_refuse (bb, "no-free-lreg", r, all[i].insn);
		      continue;
		    }
		  /* The doubled row plus every seed must still fit the
		     replay capture buffer (as checked at admission), or
		     the loop will no longer be captured.  */
		  if (!full_lane_root
		      && all.size () + 1
			 > (unsigned) XTT_DELIVERY_CAPTURE_SLOTS)
		    {
		      crp_seed_refuse (bb, "capture-budget", r, all[i].insn);
		      continue;
		    }
		  /* Emit the all-lanes preservation copy (predicated roots
		     only) and admit it into the row.  */
		  rtx_insn *seed = nullptr;
		  ls_node seed_node;
		  if (!full_lane_root)
		    {
		      seed = emit_insn_before (gen_rvtt_sfpassign
						 (gen_rtx_REG (XTT32SImode,
							       (unsigned) f),
						  gen_rtx_REG (XTT32SImode,
							       r)),
					       all[seed_pos].insn);
		      df_insn_rescan (seed);
		      if (!crp_node (bb, seed, &seed_node))
			{
			  delete_insn (seed);
			  crp_seed_refuse (bb, "word-unproven", r,
					   all[i].insn);
			  continue;
			}
		      seed_node.orig = (int) seed_pos;
		    }
		  if (!crp_apply_web_rename (all, i, extent_end, r,
					     (unsigned) f, &renames))
		    {
		      if (seed)
			delete_insn (seed);
		      crp_seed_refuse (bb, "rename-constraint", r,
				       all[i].insn);
		      continue;
		    }
		  if (seed)
		    {
		      all.insert (all.begin () + seed_pos, seed_node);
		      group.insert (group.begin () + seed_pos, seed_group);
		    }
		  ls_refresh_node_regs (all);
		  int ii = crp_current_ii ();
		  if (ii > cur_ii)
		    {
		      /* This seed does not improve the II; keep it only as a
			 possible enabler, never if it makes things
			 worse.  */
		      if (seed)
			{
			  all.erase (all.begin () + seed_pos);
			  group.erase (group.begin () + seed_pos);
			}
		      crp_undo_last_rename (&renames);
		      if (seed)
			delete_insn (seed);
		      ls_refresh_node_regs (all);
		      crp_seed_refuse (bb, "no-ii-improvement", r,
				       all[i].insn);
		      continue;
		    }
		  commits.push_back (seed);
		  if (dump_file)
		    {
		      char seed_desc[32];
		      if (seed)
			snprintf (seed_desc, sizeof seed_desc, "uid=%d",
				  INSN_UID (seed));
		      else
			snprintf (seed_desc, sizeof seed_desc,
				  "none-full-lane-root");
		      fprintf (dump_file, "Crossrow pairing seed: reg %u -> "
			       "%u web at uid=%d (%zu insns) seed %s "
			       "II %d -> %d in bb %d\n",
			       r, (unsigned) f,
			       INSN_UID (renames.back ().insns[0]),
			       renames.back ().insns.size (), seed_desc,
			       cur_ii, ii, bb->index);
		    }
		  cur_ii = ii;
		  if (cur_ii < strict_ii)
		    {
		      strict_ii = cur_ii;
		      strict_commits = commits.size ();
		    }
		  progress = true;
		}
	    }
	}
      /* Roll back everything after the last strict improvement (all of
	 it if nothing improved).  */
      if (commits.size () > strict_commits)
	{
	  rvtt_refuse (RVTT_REF_CROSSROW_PAIRING_SEED_NO_II_IMPROVEMENT,
		       dump_file,
		       "Crossrow pairing seeds rolled back: "
		       "crossrow-pairing-seed-no-ii-improvement in bb %d "
		       "(kept=%u of %zu, II %d)\n",
		       bb->index, strict_commits, commits.size (),
		       strict_ii);
	  while (commits.size () > strict_commits)
	    {
	      rtx_insn *seed = commits.back ();
	      commits.pop_back ();
	      if (seed)
		for (unsigned k = 0; k != all.size (); ++k)
		  if (all[k].insn == seed)
		    {
		      all.erase (all.begin () + k);
		      group.erase (group.begin () + k);
		      break;
		    }
	      crp_undo_last_rename (&renames);
	      if (seed)
		delete_insn (seed);
	    }
	  ls_refresh_node_regs (all);
	}
      for (rtx_insn *seed : commits)
	if (seed)
	  seed_insns.push_back (seed);
    }

  /* Shared-reload dedupe (see the comment before crp_shared_reload).
     A refusal keeps the duplicated pairing; if a later check refuses a
     committed dedupe, the whole pairing is abandoned, and since the
     deleted words were this transaction's own copies the original row
     is still restored.  */
  crp_shared_reload_info shared_reload;
  if (riscv_tt_opt_crossrow_shared_reload > 0)
    crp_shared_reload (bb, lp, all, group, copies, n, &shared_reload);

  /* Modulo variable expansion (see the comment before
     crp_mve_realize).  A refusal restores the state and the greedy path
     below proceeds; on success the order and the committed renames are
     handed over, and the renames are undone on any later refusal.  */
  std::vector<rvtt_lreg_rename_web> mve_webs;
  auto crp_undo_mve_webs = [&mve_webs, &all] ()
    {
      for (unsigned i = mve_webs.size (); i--;)
	rvtt_lreg_rename_web_undo (mve_webs[i]);
      if (!mve_webs.empty ())
	{
	  mve_webs.clear ();
	  ls_refresh_node_regs (all);
	}
    };
  bool used_mve = false;
  int mve_ii = 0, mve_place_ii = 0;
  std::vector<rtx_insn *> candidate;
  if (riscv_tt_opt_mve_expand)
    used_mve = crp_mve_expand_arm (bb, all, group, n, base_ii,
				   !seed_insns.empty ()
				   || shared_reload.reg != ~0u,
				   &candidate, &mve_ii, &mve_place_ii,
				   &mve_webs);

  /* Candidate order: the item schedule (atoms indivisible, unrenamed
     shared webs serialize).  */
  if (!used_mve)
    candidate = crp_candidate_order (all, group);
  if (!crp_order_legal_p (all, candidate))
    {
      crp_undo_mve_webs ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-order-hazard");
    }
  if (!crp_shared_reload_order_sound_p (shared_reload, candidate))
    {
      crp_undo_mve_webs ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-shared-reload-final-order");
    }

  std::vector<int> cand_index;
  for (rtx_insn *ci : candidate)
    for (unsigned i = 0; i != all.size (); ++i)
      if (all[i].insn == ci)
	{
	  cand_index.push_back (i);
	  break;
	}
  int cand_ii = cand_index.size () == all.size ()
    ? ls_cyclic_ii (all, cand_index) : base_ii;
  if (cand_ii >= base_ii)
    {
      crp_undo_mve_webs ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      if (dump_file)
	fprintf (dump_file, "Crossrow pairing refused: no modeled "
		 "steady-state II decrease in bb %d (%d -> %d)\n",
		 bb->index, base_ii, cand_ii);
      return false;
    }

  /* Record the chain for exact restoration, then commit the
     order.  */
  rtx_insn *anchor = PREV_INSN (lp.nodes[0].insn);
  std::vector<rtx_insn *> chain;
  for (rtx_insn *w = NEXT_INSN (anchor);; w = NEXT_INSN (w))
    {
      if (INSN_P (w))
	chain.push_back (w);
      if (w == lp.separator)
	break;
    }
  rtx_insn *tail = anchor;
  for (rtx_insn *ci : candidate)
    {
      if (PREV_INSN (ci) != tail)
	reorder_insns (ci, ci, tail);
      tail = ci;
    }
  if (PREV_INSN (lp.separator) != tail)
    reorder_insns (lp.separator, lp.separator, tail);

  auto crp_restore_chain = [&chain, anchor] ()
    {
      rtx_insn *at = anchor;
      for (rtx_insn *ci : chain)
	{
	  if (PREV_INSN (ci) != at)
	    reorder_insns (ci, ci, at);
	  at = ci;
	}
    };

  unsigned pads_after = ls_pad_sites (visited, bb, all);
  if (pads_after > pads_before)
    {
      crp_restore_chain ();
      crp_undo_mve_webs ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-pad-site-increase");
    }
  /* Capture-overflow check (stall-words option): the record the
     counted-loop capture sees is every row word plus every pad the NOP
     inserter still adds.  At 2n == XTT_DELIVERY_CAPTURE_SLOTS a single
     remaining pad site would make the loop too large to capture.  */
  if (riscv_tt_opt_crossrow_pairing_stall_words > 0
      && all.size () + pads_after > (unsigned) XTT_DELIVERY_CAPTURE_SLOTS)
    {
      crp_restore_chain ();
      crp_undo_mve_webs ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-capture-overflow");
    }

  /* Rewrite the shared control: the separator advances both rows at
     once and the countdown step doubles.  */
  extract_insn (lp.separator);
  validate_change (lp.separator, recog_data.operand_loc[1],
		   GEN_INT (2 * lp.dst_step), true);
  rtx cset = single_set (lp.counter);
  validate_change (lp.counter, &XEXP (SET_SRC (cset), 1), GEN_INT (-2),
		   true);
  if (!apply_change_group ())
    {
      crp_restore_chain ();
      crp_undo_mve_webs ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-control-rewrite-constraint");
    }
  df_insn_rescan (lp.separator);
  df_insn_rescan (lp.counter);

  /* The doubled separator must still be recognized as the row step,
     at twice the advance.  */
  xtt_effect_set sep_eff = rvtt_insn_effects (lp.separator);
  if (sep_eff.opaque || sep_eff.rwc.kind != xtt_rwc_effect_t::INC
      || sep_eff.rwc.dst_delta != 2 * lp.dst_step
      || sep_eff.rwc.cr_delta != 0)
    {
      extract_insn (lp.separator);
      validate_change (lp.separator, recog_data.operand_loc[1],
		       GEN_INT (lp.dst_step), true);
      cset = single_set (lp.counter);
      validate_change (lp.counter, &XEXP (SET_SRC (cset), 1), GEN_INT (-1),
		       true);
      bool restored = apply_change_group ();
      gcc_assert (restored);
      df_insn_rescan (lp.separator);
      df_insn_rescan (lp.counter);
      crp_restore_chain ();
      crp_undo_mve_webs ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-row-step-shape",
			 lp.separator);
    }

  if (dump_file)
    {
      fprintf (dump_file, "Crossrow pairing: bb %d rows=2 nodes=%zu "
	       "II %d -> %d renames=%zu seeds=%zu dst-addr=%ld/%ld "
	       "step=%ld->%ld trips=%ld->%ld target=bh\n",
	       bb->index, all.size (), base_ii, cand_ii, renames.size (),
	       seed_insns.size (),
	       (long) lp.dst_addr, (long) (lp.dst_addr + lp.dst_step),
	       (long) lp.dst_step, (long) (2 * lp.dst_step),
	       (long) lp.trips, (long) (lp.trips / 2));
      if (used_mve)
	fprintf (dump_file, "Crossrow pairing mve-expand committed: bb %d "
		 "kmin=2 place-II=%d rotation-renames=%zu realized II %d\n",
		 bb->index, mve_place_ii, mve_webs.size (), mve_ii);
    }
  return true;
}

/* Cross-row pairing driver: refresh dataflow, then try the pairing on
   every block of FN (blocks that are not self-loops are skipped
   silently).  */

static void
crossrow_pair_rows (function *fn)
{
  df_analyze ();
  std::vector<basic_block> visited;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      visited.reserve (n_basic_blocks_for_fn (fn));
      crp_pair_loop (bb, visited);
    }
}

/* Cyclic-interior region scheduling
   (-mtt-tensix-optimize-cyclic-region-schedule).

   This handles self-loop rows that issued barrier words (CC writes, Dst
   traffic, configuration accesses) split into several straight-line
   regions, which the single-region cyclic path refuses.  Barrier words
   stay in place; each interior region is list-scheduled as in the
   straight-line case, and the result is kept only if the steady-state
   initiation interval of the whole row (ls_cyclic_ii over every issued
   word of the block) strictly decreases.  Unknown latencies are taken as
   zero in both the baseline and the candidate, so they can only hide a
   stall both orders share.

   A within-region reorder is correct across iterations because region
   members only move relative to each other: a dependence between
   iterations either involves a fixed word, or connects iteration i's
   instance of the region to iteration i+1's, and every word of the
   earlier instance still precedes every word of the later one.
   Dependences within one iteration are honored by the list order.

   Refusals (the original order is kept):
     cyclic-interior-opaque-word     raw asm or opaque effects in the row
     cyclic-interior-backedge-seam   the region contains the row's first
				     or last issued word
     cyclic-interior-repeated-shape  the region signature repeats in the
				     row
     cyclic-interior-no-ii-decrease  candidate II >= current II
   plus the pad-site checks of the straight-line scheduler.  The caller
   handles replay-owner and call refusals.  */

static void
ls_schedule_cyclic_interior (basic_block bb,
			     std::vector<ls_region> &regions,
			     std::vector<basic_block> &visited)
{
  /* Whole-row model: every issued Tensix word, in order.  */
  std::vector<ls_node> body;
  for (rtx_insn *insn = BB_HEAD (bb); insn != NEXT_INSN (BB_END (bb));
       insn = NEXT_INSN (insn))
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (GET_CODE (insn) == INSN && PATTERN (insn)
	  && asm_noperands (PATTERN (insn)) >= 0)
	{
	  rvtt_refuse (RVTT_REF_CYCLIC_INTERIOR_OPAQUE_WORD, dump_file,
		       "List-schedule (cyclic-interior) refused: "
		       "cyclic-interior-opaque-word uid=%d in bb %d\n",
		       INSN_UID (insn), bb->index);
	  return;
	}
      if (GET_CODE (insn) != INSN || recog_memoized (insn) < 0
	  || get_attr_type (insn) != TYPE_TENSIX
	  || !get_attr_length (insn))
	continue;		/* scalar control or marker: no word */
      xtt_effect_set e = rvtt_insn_effects (insn);
      if (e.opaque)
	{
	  rvtt_refuse (RVTT_REF_CYCLIC_INTERIOR_OPAQUE_WORD, dump_file,
		       "List-schedule (cyclic-interior) refused: "
		       "cyclic-interior-opaque-word uid=%d in bb %d\n",
		       INSN_UID (insn), bb->index);
	  return;
	}
      ls_node nd;
      nd.insn = insn;
      nd.lat = audited_latency (insn);
      if (nd.lat < 0 || nd.lat > 1)
	nd.lat = 0;		/* lower bound, used for both orders */
      nd.words = get_attr_length (insn) / 4;
      if (e.next_slot_stall)
	/* The next-slot stall costs one extra slot per occurrence,
	   in both baseline and candidate.  */
	nd.words += 1;
      if (!collect_sfpu_regs (insn, &nd.regs))
	/* CC and store words without a result keep their LREG uses
	   for RAW ordering; they are never region members.  */
	sfpu_reg_refs (insn, &nd.regs);
      nd.raw_defs = nd.regs.defs;
      nd.orig = (int) body.size ();
      nd.cp = 0;
      nd.ready = 0;
      nd.entry_pin = 0;
      nd.pin_to_baseline = false;
      body.push_back (nd);
    }
  if (body.empty ())
    return;

  const unsigned bn = body.size ();
  std::vector<int> body_order (bn);
  for (unsigned i = 0; i != bn; ++i)
    body_order[i] = i;
  int cur_ii = ls_cyclic_ii (body, body_order);

  /* IMS does not place with unknown latencies: a word with a result
     but no known latency of at most one refuses the whole row.  */
  bool ims_row_ok = false;
  if (riscv_tt_opt_ims)
    {
      ims_row_ok = ls_ims_row_audited_p (bb);
      if (ims_row_ok && dump_file)
	{
	  rvtt_timing::mod_prob rowp
	    = rvtt_timing::make_mod_prob (ls_timing_seq (body));
	  fprintf (dump_file, "List-schedule (ims) row: bb %d words=%u "
		   "ResMII=%d RecMII=%d row-II=%d\n",
		   bb->index, bn, rvtt_timing::resmii (rowp),
		   rvtt_timing::recmii (rowp), cur_ii);
	}
    }

  for (unsigned ri = 0; ri != regions.size (); ++ri)
    {
      ls_region &r = regions[ri];
      const unsigned n = r.nodes.size ();

      /* Repeated shapes are left for the replay/MOP re-roll.  */
      bool repeated = false;
      for (unsigned rj = 0; rj != regions.size (); ++rj)
	if (rj != ri && regions[rj].signature == r.signature)
	  repeated = true;
      if (repeated)
	{
	  rvtt_refuse (RVTT_REF_CYCLIC_INTERIOR_REPEATED_SHAPE, dump_file,
		       "List-schedule (cyclic-interior) refused: "
		       "cyclic-interior-repeated-shape at uid=%d in bb %d\n",
		       INSN_UID (r.nodes[0].insn), bb->index);
	  continue;
	}

      /* Locate the region in the body model: its nodes are consecutive
	 issued words, since only barrier words separate regions.  */
      unsigned first = bn;
      for (unsigned i = 0; i != bn; ++i)
	if (body[i].insn == r.nodes[0].insn)
	  {
	    first = i;
	    break;
	  }
      gcc_assert (first != bn && first + n <= bn);
      for (unsigned k = 0; k != n; ++k)
	gcc_assert (body[first + k].insn == r.nodes[k].insn);

      /* Interior regions only: one containing the row's first or last
	 issued word sits on the backedge seam, which is not
	 modeled.  */
      if (first == 0 || first + n == bn)
	{
	  rvtt_refuse (RVTT_REF_CYCLIC_INTERIOR_BACKEDGE_SEAM, dump_file,
		       "List-schedule (cyclic-interior) refused: "
		       "cyclic-interior-backedge-seam at uid=%d in bb %d\n",
		       INSN_UID (r.nodes[0].insn), bb->index);
	  continue;
	}

      /* Under -mtt-tensix-optimize-rename-temporal, break the region's
	 storage-collision chains through rvtt_lreg_rename_chain before
	 generating candidates.  The rename service checks legality;
	 this code only selects collision roots (a region member's fresh
	 LREG definition that another issued row word also defines).
	 The whole-row acceptance below judges the result, and every
	 rename is undone if no candidate commits.  Each root is tried
	 at most once.  */
      std::vector<rvtt_lreg_rename_web> region_webs;
      auto refresh_nodes = [] (std::vector<ls_node> &nodes)
	{
	  /* As in the body model, CC and store words without a result
	     keep their LREG uses; they are never renamed, but their
	     reads order the row.  */
	  for (ls_node &nd : nodes)
	    {
	      if (!collect_sfpu_regs (nd.insn, &nd.regs))
		sfpu_reg_refs (nd.insn, &nd.regs);
	      nd.raw_defs = nd.regs.defs;
	    }
	};
      auto undo_region_renames = [&] ()
	{
	  if (region_webs.empty ())
	    return;
	  for (unsigned i = region_webs.size (); i--;)
	    rvtt_lreg_rename_web_undo (region_webs[i]);
	  if (dump_file)
	    fprintf (dump_file, "List-schedule (interior-rename): undid "
		     "%zu chain rename(s) in bb %d region at uid=%d\n",
		     region_webs.size (), bb->index,
		     INSN_UID (r.nodes[0].insn));
	  region_webs.clear ();
	  refresh_nodes (r.nodes);
	  refresh_nodes (body);
	};
      if (riscv_tt_opt_rename_temporal
	  && (riscv_tt_opt_cyclic_region_schedule
	      || (riscv_tt_opt_ims && ims_row_ok)))
	{
	  /* Renames committed for earlier regions may have moved webs.  */
	  refresh_nodes (r.nodes);
	  unsigned pads_pristine = ls_pad_sites (visited, bb, r.nodes);
	  for (unsigned k = 0; k != n; ++k)
	    for (unsigned reg = SFPU_REG_FIRST; reg <= SFPU_REG_LAST; ++reg)
	      {
		if (!TEST_HARD_REG_BIT (r.nodes[k].raw_defs, reg)
		    || TEST_HARD_REG_BIT (r.nodes[k].regs.uses, reg))
		  continue;	/* not a fresh-definition root */
		bool collision = false;
		for (unsigned j = 0; j != bn && !collision; ++j)
		  if (j != first + k
		      && TEST_HARD_REG_BIT (body[j].raw_defs, reg))
		    collision = true;
		if (!collision)
		  continue;
		rvtt_lreg_rename_web web;
		if (!rvtt_lreg_rename_chain (bb, r.nodes[k].insn, -1, &web))
		  break;	/* refused by the rename service */
		region_webs.push_back (web);
		refresh_nodes (r.nodes);
		refresh_nodes (body);
		if (dump_file)
		  fprintf (dump_file, "List-schedule (interior-rename): "
			   "chain L%d -> L%d at uid=%d in bb %d region at "
			   "uid=%d\n", web.old_l, web.new_l,
			   INSN_UID (r.nodes[k].insn), bb->index,
			   INSN_UID (r.nodes[0].insn));
		break;		/* one rename attempt per root insn */
	      }
	  if (!region_webs.empty ()
	      && ls_pad_sites (visited, bb, r.nodes) > pads_pristine)
	    {
	      undo_region_renames ();
	      rvtt_refuse (RVTT_REF_PAD_SITE, dump_file,
			   "List-schedule (interior-rename) refused: "
			   "pad-site increase, undid renames in bb %d "
			   "region at uid=%d\n",
			   bb->index, INSN_UID (r.nodes[0].insn));
	    }
	}

      /* Entry pins as in the straight-line scheduler, over the region's
	 acyclic baseline (for candidate construction only).  */
      insn_regs ep_regs;
      CLEAR_HARD_REG_SET (ep_regs.uses);
      CLEAR_HARD_REG_SET (ep_regs.defs);
      int ep_lat = 0;
      if (r.entry_producer)
	{
	  sfpu_reg_refs (r.entry_producer, &ep_regs);
	  ep_lat = audited_latency (r.entry_producer);
	  if (ep_lat < 0 || ep_lat > 1)
	    ep_lat = 0;
	}
      for (unsigned i = 0; i != n; ++i)
	{
	  r.nodes[i].entry_pin = 0;
	  r.nodes[i].pin_to_baseline
	    = hard_reg_set_intersect_p (r.unaudited_defs,
					r.nodes[i].regs.uses)
	      || hard_reg_set_intersect_p (r.unaudited_defs,
					   r.nodes[i].raw_defs);
	  if (r.entry_producer
	      && (hard_reg_set_intersect_p (ep_regs.defs,
					    r.nodes[i].regs.uses)
		  || hard_reg_set_intersect_p (ep_regs.defs,
					       r.nodes[i].raw_defs))
	      && ep_lat > r.nodes[i].entry_pin)
	    r.nodes[i].entry_pin = ep_lat;
	}
      rtx_insn *exit_consumer
	= next_issued_insn (bb, r.nodes[n - 1].insn);
      std::vector<bool> exit_shadow (n, false);
      if (exit_consumer)
	{
	  insn_regs xc;
	  sfpu_reg_refs (exit_consumer, &xc);
	  HARD_REG_SET wanted = xc.uses;
	  wanted |= xc.defs;
	  for (unsigned i = 0; i != n; ++i)
	    exit_shadow[i]
	      = hard_reg_set_intersect_p (r.nodes[i].raw_defs, wanted);
	}
      else
	for (unsigned i = 0; i != n; ++i)
	  exit_shadow[i] = true;
      std::vector<int> base_order (n);
      for (unsigned i = 0; i != n; ++i)
	base_order[i] = i;
      std::vector<int> base_issue (n, 0);
      ls_simulate (r.nodes, base_order, &base_issue, exit_shadow);
      for (unsigned i = 0; i != n; ++i)
	if (r.nodes[i].pin_to_baseline)
	  r.nodes[i].entry_pin = base_issue[i];

      /* Candidates, all judged on the whole row's steady-state II: the
	 list order and the IMS order.  */
      std::vector<int> order;
      int cand_ii = INT_MAX;
      bool used_ims = false;
      if (riscv_tt_opt_cyclic_region_schedule)
	{
	  order = ls_list_order (r.nodes);
	  std::vector<int> cb (body_order);
	  for (unsigned k = 0; k != n; ++k)
	    cb[first + k] = (int) (first + order[k]);
	  cand_ii = ls_cyclic_ii (body, cb);
	}
      if (riscv_tt_opt_ims && ims_row_ok)
	{
	  ls_ims_candidate ims;
	  if (ls_ims_order (bb, r.nodes, cur_ii, false, &ims))
	    {
	      std::vector<int> cb (body_order);
	      for (unsigned k = 0; k != n; ++k)
		cb[first + k] = (int) (first + ims.order[k]);
	      int ims_ii = ls_cyclic_ii (body, cb);
	      if (dump_file)
		fprintf (dump_file, "List-schedule (ims) candidate: bb %d "
			 "region at uid=%d modeled row II %d (place-II %d "
			 "MII %d)\n",
			 bb->index, INSN_UID (r.nodes[0].insn), ims_ii,
			 ims.place_ii, ims.mii);
	      if (ims_ii < cand_ii)
		{
		  order = ims.order;
		  cand_ii = ims_ii;
		  used_ims = true;
		}
	    }
	}
      if (!region_webs.empty ())
	{
	  /* The renames alone, with the region's current order, are
	     also a candidate.  */
	  int ident_ii = ls_cyclic_ii (body, body_order);
	  if (ident_ii < cand_ii)
	    {
	      order.resize (n);
	      for (unsigned k = 0; k != n; ++k)
		order[k] = (int) k;
	      cand_ii = ident_ii;
	      used_ims = false;
	    }
	}
      if (order.empty ())
	{
	  undo_region_renames ();
	  continue;	/* every candidate was refused */
	}
      if (cand_ii >= cur_ii)
	{
	  undo_region_renames ();
	  if (riscv_tt_opt_cyclic_region_schedule)
	    rvtt_refuse (RVTT_REF_CYCLIC_INTERIOR_NO_II_DECREASE, dump_file,
			 "List-schedule (cyclic-interior) refused: "
			 "cyclic-interior-no-ii-decrease at uid=%d in bb %d "
			 "(%d -> %d)\n",
			 INSN_UID (r.nodes[0].insn), bb->index, cur_ii,
			 cand_ii);
	  else
	    rvtt_refuse (RVTT_REF_IMS_NO_II_DECREASE, dump_file,
			 "List-schedule (ims) refused: ims-no-ii-decrease "
			 "at uid=%d in bb %d (%d -> %d)\n",
			 INSN_UID (r.nodes[0].insn), bb->index, cur_ii,
			 cand_ii);
	  continue;
	}

      /* Commit checks: the NOP inserter's pad-site count and the entry
	 producer's pad state, as in the straight-line scheduler.  */
      unsigned pads_before = ls_pad_sites (visited, bb, r.nodes);
      bool ep_dynamic
	= r.entry_producer
	  && get_attr_xtt_delay (r.entry_producer) == XTT_DELAY_DYNAMIC;
      bool ep_needed_before
	= ep_dynamic
	  && delay_nop_needed_p (visited, bb, r.entry_producer,
				 XTT_DELAY_DYNAMIC);

      /* Record for exact restoration, debug insns included.  */
      std::vector<rtx_insn *> chain;
      for (rtx_insn *w = NEXT_INSN (r.anchor);; w = NEXT_INSN (w))
	{
	  if (INSN_P (w))
	    chain.push_back (w);
	  if (w == r.nodes[n - 1].insn)
	    break;
	}

      rtx_insn *after = r.anchor;
      for (unsigned k = 0; k != n; ++k)
	{
	  rtx_insn *insn = r.nodes[order[k]].insn;
	  if (PREV_INSN (insn) != after)
	    reorder_insns (insn, insn, after);
	  after = insn;
	}

      unsigned pads_after = ls_pad_sites (visited, bb, r.nodes);
      bool ep_flipped
	= ep_dynamic && !ep_needed_before
	  && delay_nop_needed_p (visited, bb, r.entry_producer,
				 XTT_DELAY_DYNAMIC);
      if (pads_after > pads_before || ep_flipped)
	{
	  after = r.anchor;
	  for (rtx_insn *insn : chain)
	    {
	      if (PREV_INSN (insn) != after)
		reorder_insns (insn, insn, after);
	      after = insn;
	    }
	  undo_region_renames ();
	  if (dump_file)
	    fprintf (dump_file, "List-schedule (cyclic-interior) refused: "
		     "%s, restored bb %d region at uid=%d\n",
		     ep_flipped ? "entry-producer pad flip"
				: "pad-site increase",
		     bb->index, INSN_UID (r.nodes[0].insn));
	  continue;
	}

      if (dump_file)
	{
	  if (!region_webs.empty ())
	    fprintf (dump_file, "List-schedule (interior-rename): committed "
		     "%zu chain rename(s) in bb %d region at uid=%d\n",
		     region_webs.size (), bb->index,
		     INSN_UID (r.nodes[0].insn));
	  fprintf (dump_file, "List-schedule (%s): bb %d "
		   "region at uid=%d nodes=%u row II %d -> %d "
		   "target=%s\n",
		   used_ims ? "ims-interior" : "cyclic-interior",
		   bb->index, INSN_UID (r.nodes[0].insn), n, cur_ii,
		   cand_ii, TARGET_XTT_TENSIX_WH ? "wh" : "bh");
	  for (unsigned k = 0; k != n; ++k)
	    fprintf (dump_file, "List-schedule slot-order=%u uid=%d\n",
		     k, INSN_UID (r.nodes[order[k]].insn));
	}
      for (unsigned k = 0; k != n; ++k)
	body_order[first + k] = (int) (first + order[k]);
      cur_ii = cand_ii;
    }
}

/* Region scheduler driver over FN.  Targets without latency data are
   refused.  In each block, collect candidate regions of admissible nodes
   between barriers (an explicit replay owner ends the block's
   eligibility).  A self-loop block goes to the cyclic paths under their
   flags (the single-region path when the row is one region with no seam
   hazards, else the cyclic-interior path) and is skipped otherwise.  In
   other blocks, exactly two isomorphic regions are scheduled as a pair,
   larger families are left to replay formation, and single regions are
   list-scheduled.  */

static void
list_schedule_regions (function *fn)
{
  if (!TARGET_XTT_TENSIX_BH && !TARGET_XTT_TENSIX_WH)
    {
      if (dump_file)
	fprintf (dump_file, "List-schedule refused: no audited latency "
		 "facts for this target\n");
      return;
    }

  df_analyze ();

  std::vector<basic_block> visited;
  visited.reserve (n_basic_blocks_for_fn (fn));

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      /* A self-loop row executes back-to-back across the backedge, so
	 the linear boundary model mispredicts the seam; capture rotation
	 handles that case.  */
      bool self_loop = false;
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (e->dest == bb)
	  self_loop = true;
      if (self_loop && !riscv_tt_opt_round_interleave
	  && !riscv_tt_opt_cyclic_region_schedule
	  && !riscv_tt_opt_ims)
	{
	  if (dump_file)
	    fprintf (dump_file, "List-schedule deferred: cyclic row "
		     "adjacency in bb %d (capture rotation owns the "
		     "backedge seam)\n", bb->index);
	  continue;
	}

      /* Collect the block's candidate regions.  */
      std::vector<ls_region> regions;
      std::vector<ls_node> nodes;
      rtx_insn *anchor = nullptr;
      rtx_insn *entry_producer = nullptr;
      HARD_REG_SET region_unaudited;
      CLEAR_HARD_REG_SET (region_unaudited);
      bool stop_block = false;
      unsigned tensix_barriers = 0;	/* issued Tensix words outside any
					   region (seam hazards for the
					   cyclic path) */
      bool bb_has_call = false;

      auto flush = [&] ()
      {
	/* A two-node region is either fixed by a dependence or
	   symmetric under the objective, so regions below three nodes
	   are skipped.  */
	if (nodes.size () == 2 && dump_file)
	  rvtt_refuse (RVTT_REF_TWO_NODE, dump_file,
		       "List-schedule skipped: two-node region at "
		       "uid=%d in bb %d (below the interleave minimum)\n",
		       INSN_UID (nodes[0].insn), bb->index);
	if (nodes.size () >= 3)
	  {
	    ls_region r;
	    r.nodes = std::move (nodes);
	    r.anchor = anchor;
	    r.entry_producer = entry_producer;
	    r.unaudited_defs = region_unaudited;
	    for (const ls_node &nd : r.nodes)
	      r.signature.push_back (INSN_CODE (nd.insn));
	    regions.push_back (std::move (r));
	  }
	nodes.clear ();
      };

      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (stop_block)
	    break;
	  if (!NONDEBUG_INSN_P (insn))
	    continue;

	  ls_node node;
	  const char *why = nullptr;
	  if (ls_admissible_p (insn, &node, &why))
	    {
	      if (nodes.empty ())
		{
		  anchor = PREV_INSN (insn);
		  entry_producer = ls_entry_producer (bb, insn);
		  /* All admitted latencies are at most one, so only the
		     immediately preceding issued instruction can still
		     have a pending result.  If its latency is unknown or
		     larger, its defs become the pin hazard instead.
		     Zero-length markers issue no word and are not
		     producers.  */
		  CLEAR_HARD_REG_SET (region_unaudited);
		  if (entry_producer)
		    {
		      int ep_lat = audited_latency (entry_producer);
		      if (ep_lat < 0 || ep_lat > 1)
			{
			  insn_regs epr;
			  sfpu_reg_refs (entry_producer, &epr);
			  region_unaudited = epr.defs;
			}
		    }
		}
	      node.orig = (int) nodes.size ();
	      nodes.push_back (node);
	      continue;
	    }

	  /* Barrier.  */
	  if (GET_CODE (insn) == INSN
	      && recog_memoized (insn) >= 0
	      && get_attr_type (insn) == TYPE_TENSIX
	      && get_attr_length (insn))
	    {
	      ++tensix_barriers;
	      if (dump_file)
		fprintf (dump_file, "List-schedule barrier: %s uid=%d\n",
			 why, INSN_UID (insn));
	    }
	  else if (GET_CODE (insn) == INSN && PATTERN (insn)
		   && asm_noperands (PATTERN (insn)) >= 0)
	    /* Raw assembly may issue Tensix words the effect model
	       cannot see: a seam hazard for the cyclic path.  */
	    ++tensix_barriers;
	  if (CALL_P (insn))
	    bb_has_call = true;
	  flush ();
	  if (GET_CODE (insn) == INSN && recog_memoized (insn) >= 0
	      && get_attr_type (insn) == TYPE_TENSIX
	      && get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
	    stop_block = true;	/* as for a replay owner */
	}
      if (!stop_block)
	flush ();

      /* Round-interleave: a self-loop row that is a single region,
	 with no other issued Tensix word, replay owner or call in the
	 block, is scheduled under the wrapped steady-state II model;
	 other self-loop shapes are skipped.  */
      if (self_loop)
	{
	  const char *why_c = nullptr;
	  if (stop_block)
	    why_c = "round-interleave-replay-owner-in-row";
	  else if (bb_has_call)
	    why_c = "round-interleave-call-in-row";
	  else if (tensix_barriers)
	    why_c = "round-interleave-seam-barrier-word";
	  else if (regions.size () != 1)
	    why_c = "round-interleave-row-not-one-region";
	  if (!why_c && (riscv_tt_opt_round_interleave || riscv_tt_opt_ims))
	    {
	      ls_schedule_region_cyclic (bb, regions[0].nodes,
					 regions[0].anchor, visited);
	      continue;
	    }
	  /* Cyclic-interior: the multi-region self-loop shapes (and,
	     without round-interleave, every self-loop shape) have their
	     interior regions scheduled under the whole-row II.  Replay
	     owners and calls are still refused.  */
	  if ((riscv_tt_opt_cyclic_region_schedule || riscv_tt_opt_ims)
	      && !stop_block && !bb_has_call)
	    {
	      ls_schedule_cyclic_interior (bb, regions, visited);
	      continue;
	    }
	  if (dump_file)
	    fprintf (dump_file, "List-schedule deferred: cyclic row "
		     "adjacency in bb %d (%s)\n", bb->index,
		     why_c ? why_c : "round-interleave-flag-off");
	  continue;
	}

      /* Repeated region shapes are skipped so unrolled row copies stay
	 textually identical for the replay and MOP re-roll.  Under
	 round-interleave, exactly two copies are scheduled as a pair
	 (see ls_schedule_iso_pair).  */
      std::vector<bool> pair_done (regions.size (), false);
      for (unsigned i = 0; i != regions.size (); ++i)
	{
	  if (pair_done[i])
	    continue;
	  unsigned nmatch = 0;
	  unsigned mate = 0;
	  for (unsigned j = 0; j != regions.size (); ++j)
	    if (j != i && regions[j].signature == regions[i].signature)
	      {
		if (!nmatch)
		  mate = j;
		++nmatch;
	      }
	  if (nmatch)
	    {
	      if (riscv_tt_opt_round_interleave && nmatch == 1
		  && mate > i)
		{
		  pair_done[mate] = true;
		  ls_schedule_iso_pair (bb, regions[i], regions[mate],
					visited);
		  continue;
		}
	      rvtt_refuse (RVTT_REF_REPEATED_ROW, dump_file,
			   "List-schedule deferred: repeated-row "
			   "shape at uid=%d in bb %d (replay capture "
			   "formation owns row isomorphism)\n",
			   INSN_UID (regions[i].nodes[0].insn), bb->index);
	      continue;
	    }
	  if (!riscv_tt_opt_list_schedule)
	    continue;	/* round-interleave does not apply to single regions */
	  ls_schedule_region (bb, regions[i].nodes, regions[i].anchor,
			      regions[i].entry_producer,
			      next_issued_insn
				(bb, regions[i].nodes.back ().insn),
			      regions[i].unaudited_defs, visited);
	}
    }
}

/* Capture rotation: cross-row interlock filling.

   fill_interlock_shadows fills a stall with an instruction from the same
   row.  A counted row loop that replay formation captures plays back
   back-to-back, so the row is a cycle: its last instruction issues right
   before the next playback's first, and independent fillers also exist
   across the row boundary.  This phase performs two cyclic reorders:

   - Seam fill: when the row's last issued word has a one-slot result
     latency and the row's first word consumes one of its destinations
     (a back-to-back stall across playbacks), an independent row member
     moves to the row's tail or head.  This is a reorder within one
     iteration, with no prologue or epilogue.  It is done even where the
     DYNAMIC probe fires (WH), since no in-row mechanism reaches across
     the backedge; the probe must be quiet after the move.

   - Prologue rotation: a filler whose inputs are all loop-invariant (an
     immediate load, a copy from an invariant register) may move forward
     past its own consumers into a stalled gap.  Consumers between the
     old and new positions then read the previous iteration's instance,
     which has the same value.  The first row has no previous instance,
     so a copy of the filler is emitted in the loop's preheader; no
     epilogue is needed.  Requirements:
       . no CC write, configuration access, RWC step or Dst traffic (the
	 bare unpredicated copy is exempt), and a result latency of zero;
       . a lane-predicated filler is allowed only if no row member writes
	 CC, so the CC state is the same on every trip and the prologue
	 copy runs under the same state; the bare all-lanes copy needs no
	 such check;
       . every input register is invariant in the row, which also
	 excludes read-modify-write forms;
       . the filler is the only writer of its destination in the row;
       . no row member before the filler reads the destination (it would
	 see the prologue's value instead of the previous row's);
       . the destination is not live into the row header;
       . every crossed instruction satisfies shadow_crossing_safe_p;
       . the loop has a dedicated preheader.

   Rotation never adds a row word (the prologue copy runs once per run,
   outside the capture), so any strict decrease of the modeled cyclic
   stall count is a gain.  The accounting covers every adjacency a move
   changes, including the vacated position and the row seam; any term
   with an unknown latency refuses.  Required-NOP sites inside the row are
   left to the NOP inserter and fill_nop_shadows.

   The accepted row shape matches the counted-loop replay hoist: a
   single-block counted loop of replay-safe Tensix words, optionally one
   trailing TTINCRWC, one scalar counter step, and the final conditional
   jump.  */

struct rotation_row
{
  basic_block bb;
  std::vector<rtx_insn *> issued; /* issued Tensix words, in order */
};

/* Return the predecessor of self-loop BB other than itself if it is a
   dedicated preheader (single successor, no abnormal edge), else
   null.  */

static basic_block
rotation_dedicated_preheader (basic_block bb)
{
  if (EDGE_COUNT (bb->preds) != 2)
    return nullptr;
  basic_block pre = nullptr;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->preds)
    if (e->src != bb)
      pre = e->src;
  if (!pre || pre == ENTRY_BLOCK_PTR_FOR_FN (cfun)
      || !single_succ_p (pre) || single_succ (pre) != bb
      || (single_succ_edge (pre)->flags & EDGE_ABNORMAL))
    return nullptr;
  return pre;
}

/* Return true if BB is a self-loop with the capturable-row shape.  If BB
   is a self-loop of another shape, return false with *REASON set;
   *REASON stays null when BB is not a self-loop.  */

static bool
rotation_row_p (basic_block bb, rotation_row *row, const char **reason)
{
  *reason = nullptr;

  bool self = false;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (e->dest == bb)
      self = true;
  if (!self)
    return false;
  if (EDGE_COUNT (bb->succs) != 2 || EDGE_COUNT (bb->preds) != 2)
    {
      *reason = "self-loop without the two-predecessor/two-successor "
		"row shape";
      return false;
    }

  row->bb = bb;
  row->issued.clear ();
  bool saw_scalar = false;
  bool saw_trailing_increment = false;

  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (JUMP_P (insn))
	{
	  if (insn != BB_END (bb))
	    {
	      *reason = "control flow inside the row";
	      return false;
	    }
	  continue;
	}
      if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0)
	{
	  *reason = "opaque payload";
	  return false;
	}
      if (GET_CODE (PATTERN (insn)) == USE
	  || GET_CODE (PATTERN (insn)) == CLOBBER)
	continue;
      if (recog_memoized (insn) >= 0 && get_attr_type (insn) == TYPE_TENSIX)
	{
	  if (!get_attr_length (insn))
	    continue; /* zero-length marker */
	  if (get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
	    {
	      *reason = "explicit replay owner";
	      return false;
	    }
	  if (saw_scalar)
	    {
	      *reason = "scalar payload inside the row";
	      return false;
	    }
	  if (recog_memoized (insn) == CODE_FOR_rvtt_ttincrwc)
	    {
	      if (saw_trailing_increment || row->issued.empty ())
		{
		  *reason = "untyped row-step shape";
		  return false;
		}
	      saw_trailing_increment = true;
	    }
	  else if (get_attr_xtt_replay (insn) != XTT_REPLAY_SAFE
		   || saw_trailing_increment)
	    {
	      *reason = "non-capturable word";
	      return false;
	    }
	  row->issued.push_back (insn);
	  continue;
	}
      /* Scalar insns: accept only the loop's counter step.  */
      rtx set = single_set (insn);
      if (saw_scalar || !set || !REG_P (SET_DEST (set))
	  || SFPU_REG_P (REGNO (SET_DEST (set)))
	  || contains_mem_rtx_p (PATTERN (insn)))
	{
	  *reason = "scalar payload beyond the counter";
	  return false;
	}
      saw_scalar = true;
    }

  if (row->issued.size () < 4)
    {
      *reason = "row too short to capture";
      return false;
    }
  return true;
}

/* Target name used in capture-rotation dump lines ("wh" or "bh").  */

static const char *
rotation_target_name ()
{
  return TARGET_XTT_TENSIX_WH ? "wh" : "bh";
}

/* Return the first issued Tensix insn in [FROM, TO] that a filler of
   class HIDDEN_FREE may not cross, or null if there is none.  Register
   references of markers are added to *CROSSED.  */

static rtx_insn *
rotation_crossed_segment (rtx_insn *from, rtx_insn *to, bool hidden_free,
			  insn_regs *crossed)
{
  CLEAR_HARD_REG_SET (crossed->uses);
  CLEAR_HARD_REG_SET (crossed->defs);
  for (rtx_insn *x = from; x != NEXT_INSN (to); x = NEXT_INSN (x))
    {
      if (!NONDEBUG_INSN_P (x))
	continue;
      insn_regs x_regs;
      sfpu_reg_refs (x, &x_regs);
      crossed->uses |= x_regs.uses;
      crossed->defs |= x_regs.defs;
      if (GET_CODE (PATTERN (x)) == USE
	  || GET_CODE (PATTERN (x)) == CLOBBER
	  || (recog_memoized (x) >= 0
	      && get_attr_type (x) == TYPE_TENSIX
	      && !get_attr_length (x)))
	continue;
      if (!issued_tensix_p (x) || !shadow_crossing_safe_p (x, hidden_free))
	return x;
    }
  return nullptr;
}

/* Wider filler pool for the plain-reorder movers.

   The seam fill and interior gap fill only change the issue order within
   an iteration, so they may also move two classes of words with hidden
   state, provided no crossed word interacts with that state:

   - a Dst load or store with a known result latency and no RWC step of
     its own, if no crossed word touches Dst or steps an RWC counter: it
     then accesses the same Dst rows at its new position;
   - the TTINCRWC row step, if no crossed word touches Dst or the RWC
     state.  While replay hoisting is enabled this class is refused:
     counted_loop_payload (rtl-rvtt-replay.cc) rejects any loop whose
     TTINCRWC is not the last word, so moving it would lose the capture.

   The prologue mover keeps the pure-LREG pool: its copy runs once
   outside the row, which is only correct for row-invariant values, not
   for a Dst access or RWC step executed twice.  */

enum rotation_filler_kind
{
  ROT_FILLER_REFUSED,
  ROT_FILLER_LREG,	/* the shadow_filler_p classes */
  ROT_FILLER_DST,	/* Dst load/store without an RWC step */
  ROT_FILLER_RWC_STEP	/* the TTINCRWC row step */
};

/* Classify INSN for the plain-reorder movers.  *WHY names the refusal
   for the wider classes; refusals of the pure-LREG classes are not
   reported.  */

static rotation_filler_kind
rotation_filler_kind_p (rtx_insn *insn, insn_regs *regs, bool *hidden_free,
			const char **why)
{
  *why = nullptr;
  if (shadow_filler_p (insn, regs, hidden_free))
    return ROT_FILLER_LREG;
  *hidden_free = false;
  if (JUMP_P (insn) || !issued_tensix_p (insn)
      || contains_mem_rtx_p (PATTERN (insn)))
    return ROT_FILLER_REFUSED;
  /* Every register reference must be an SFPU register, but unlike
     collect_sfpu_regs the wider classes need no LREG destination:
     stores and the row step define nothing.  */
  CLEAR_HARD_REG_SET (regs->uses);
  CLEAR_HARD_REG_SET (regs->defs);
  for (df_ref ref = DF_INSN_USES (insn); ref; ref = DF_REF_NEXT_LOC (ref))
    {
      unsigned regno = DF_REF_REGNO (ref);
      if (regno >= FIRST_PSEUDO_REGISTER || !SFPU_REG_P (regno))
	return ROT_FILLER_REFUSED;
      SET_HARD_REG_BIT (regs->uses, regno);
    }
  for (df_ref ref = DF_INSN_DEFS (insn); ref; ref = DF_REF_NEXT_LOC (ref))
    {
      unsigned regno = DF_REF_REGNO (ref);
      if (regno >= FIRST_PSEUDO_REGISTER || !SFPU_REG_P (regno))
	return ROT_FILLER_REFUSED;
      SET_HARD_REG_BIT (regs->defs, regno);
    }
  /* A CC-predicated write also reads its destination.  */
  regs->uses |= regs->defs;
  xtt_effect_set e = rvtt_insn_effects (insn);
  if (e.opaque || e.cc_write
      || e.config_dests_written || e.config_dests_read)
    return ROT_FILLER_REFUSED;
  if (e.dst_mem_read || e.dst_mem_write)
    {
      if (e.rwc.kind != xtt_rwc_effect_t::NONE)
	{
	  *why = "carries a non-neutral or unaudited RWC mode on a "
		 "Dst access";
	  return ROT_FILLER_REFUSED;
	}
      return ROT_FILLER_DST;
    }
  if (e.rwc.kind == xtt_rwc_effect_t::INC)
    return ROT_FILLER_RWC_STEP;
  return ROT_FILLER_REFUSED;
}

/* Return true if a Dst-touching or RWC-stepping filler may cross INSN:
   known effects, no CC write, and no Dst, RWC or memory access.  A
   Dst-reading filler does not cross another Dst reader either.  */

static bool
rotation_dst_rwc_crossing_safe_p (rtx_insn *x)
{
  if (get_attr_xtt_replay (x) == XTT_REPLAY_OWNER
      || contains_mem_rtx_p (PATTERN (x)))
    return false;
  xtt_effect_set e = rvtt_insn_effects (x);
  return !e.opaque && !e.cc_write
    && !e.config_dests_written && !e.config_dests_read
    && !e.dst_mem_read && !e.dst_mem_write
    && e.rwc.kind == xtt_rwc_effect_t::NONE;
}

/* Crossing walk that applies shadow_crossing_safe_p for the pure-LREG
   classes and rot_dst_rwc_crossing_safe_p for the wider ones.  */

static rtx_insn *
rotation_crossed_segment_kind (rtx_insn *from, rtx_insn *to,
			       rotation_filler_kind kind, bool hidden_free,
			       insn_regs *crossed)
{
  if (kind == ROT_FILLER_LREG)
    return rotation_crossed_segment (from, to, hidden_free, crossed);
  CLEAR_HARD_REG_SET (crossed->uses);
  CLEAR_HARD_REG_SET (crossed->defs);
  for (rtx_insn *x = from; x != NEXT_INSN (to); x = NEXT_INSN (x))
    {
      if (!NONDEBUG_INSN_P (x))
	continue;
      insn_regs x_regs;
      sfpu_reg_refs (x, &x_regs);
      crossed->uses |= x_regs.uses;
      crossed->defs |= x_regs.defs;
      if (GET_CODE (PATTERN (x)) == USE
	  || GET_CODE (PATTERN (x)) == CLOBBER
	  || (recog_memoized (x) >= 0
	      && get_attr_type (x) == TYPE_TENSIX
	      && !get_attr_length (x)))
	continue;
      if (!issued_tensix_p (x) || !rotation_dst_rwc_crossing_safe_p (x))
	return x;
    }
  return nullptr;
}

/* After a move, check that no new DYNAMIC-delay pad site appears at
   the producer, the filler, or the filler's old predecessor, as in
   fill_nop_shadows.  */

static bool
rotation_delay_clean_p (std::vector<basic_block> &visited, basic_block bb,
			rtx_insn *producer, rtx_insn *cand, rtx_insn *prev,
			bool prev_needed_before)
{
  if (get_attr_xtt_delay (producer) == XTT_DELAY_DYNAMIC
      && delay_nop_needed_p (visited, bb, producer, XTT_DELAY_DYNAMIC))
    return false;
  if (get_attr_xtt_delay (cand) == XTT_DELAY_DYNAMIC
      && delay_nop_needed_p (visited, bb, cand, XTT_DELAY_DYNAMIC))
    return false;
  if (prev && get_attr_xtt_delay (prev) == XTT_DELAY_DYNAMIC
      && !prev_needed_before
      && delay_nop_needed_p (visited, bb, prev, XTT_DELAY_DYNAMIC))
    return false;
  return true;
}

/* Close the row's seam stall (last issued word to the first word of the
   next playback) by moving one independent member to the tail or the
   head.  */

static bool
rotate_seam_fill (rotation_row const &row, std::vector<basic_block> &visited)
{
  auto const &issued = row.issued;
  unsigned m = issued.size ();
  rtx_insn *last = issued[m - 1];
  rtx_insn *first = issued[0];

  int s_seam = adjacency_stall (last, first);
  if (s_seam < 0)
    {
      if (dump_file)
	fprintf (dump_file, "Capture rotation refused: unaudited result "
		 "latency at the seam of bb %d\n", row.bb->index);
      return false;
    }
  if (s_seam == 0)
    {
      if (dump_file)
	fprintf (dump_file, "Capture rotation: no modeled seam stall in "
		 "bb %d\n", row.bb->index);
      return false;
    }
  /* A STATIC delay pads before any non-NOP word, so no filler can
     close it, and a pad at the tail would undo the fill.  */
  if (get_attr_xtt_delay (last) == XTT_DELAY_STATIC)
    return false;
  if (dump_file)
    fprintf (dump_file, "Capture rotation: modeled seam stall after uid=%d "
	     "in bb %d\n", INSN_UID (last), row.bb->index);

  for (unsigned dir = 0; dir != 2; ++dir)
    for (unsigned o = dir ? 1 : m - 2; dir ? o <= m - 2 : o != 0;
	 dir ? ++o : --o)
      {
	rtx_insn *cand = issued[o];
	insn_regs cand_regs;
	bool hidden_free;
	const char *kind_why;
	rotation_filler_kind kind
	  = rotation_filler_kind_p (cand, &cand_regs, &hidden_free,
				    &kind_why);
	if (kind == ROT_FILLER_REFUSED)
	  {
	    if (kind_why && dump_file)
	      fprintf (dump_file, "Capture rotation refused: filler uid=%d "
		       "%s\n", INSN_UID (cand), kind_why);
	    continue;
	  }
	if (kind == ROT_FILLER_RWC_STEP && riscv_tt_opt_replay_hoist)
	  {
	    rvtt_refuse (RVTT_REF_ROW_STEP, dump_file,
			 "Capture rotation refused: row-step "
			 "filler uid=%d deferred to replay capture "
			 "formation\n", INSN_UID (cand));
	    continue;
	  }
	if (audited_latency (cand) != 0
	    /* A STATIC-delay filler would bring its pad into the seam slot.  */
	    || get_attr_xtt_delay (cand) == XTT_DELAY_STATIC)
	  continue;

	insn_regs crossed;
	rtx_insn *blocker
	  = dir ? rotation_crossed_segment_kind (first, PREV_INSN (cand),
						 kind, hidden_free, &crossed)
		: rotation_crossed_segment_kind (NEXT_INSN (cand), last,
						 kind, hidden_free, &crossed);
	if (blocker)
	  {
	    if (dump_file)
	      fprintf (dump_file, "Capture rotation refused: filler uid=%d "
		       "cannot cross uid=%d\n",
		       INSN_UID (cand), INSN_UID (blocker));
	    continue;
	  }
	if (hard_reg_set_intersect_p (cand_regs.uses, crossed.defs)
	    || hard_reg_set_intersect_p (cand_regs.defs, crossed.uses)
	    || hard_reg_set_intersect_p (cand_regs.defs, crossed.defs))
	  continue;

	rtx_insn *prev = issued[o - 1];
	rtx_insn *next = issued[o + 1];
	int s_prev_cand = adjacency_stall (prev, cand);
	int s_cand_next = adjacency_stall (cand, next);
	int s_prev_next = adjacency_stall (prev, next);
	int s_last_cand = adjacency_stall (last, cand);
	int s_cand_first = adjacency_stall (cand, first);
	if (s_prev_cand < 0 || s_cand_next < 0 || s_prev_next < 0
	    || s_last_cand < 0 || s_cand_first < 0)
	  {
	    if (dump_file)
	      fprintf (dump_file, "Capture rotation refused: unaudited "
		       "latency at the vacated seam of uid=%d\n",
		       INSN_UID (cand));
	    continue;
	  }
	if (s_prev_next + s_last_cand + s_cand_first
	    >= s_prev_cand + s_cand_next + s_seam)
	  {
	    if (dump_file)
	      fprintf (dump_file, "Capture rotation refused: no modeled "
		       "stall decrease rotating uid=%d\n", INSN_UID (cand));
	    continue;
	  }

	bool prev_needed_before
	  = (get_attr_xtt_delay (prev) == XTT_DELAY_DYNAMIC
	     && delay_nop_needed_p (visited, row.bb, prev,
				    XTT_DELAY_DYNAMIC));
	rtx_insn *restore_after = PREV_INSN (cand);
	int cand_uid = INSN_UID (cand);

	reorder_insns (cand, cand, dir ? PREV_INSN (first) : last);
	if (rotation_delay_clean_p (visited, row.bb, last, cand, prev,
				    prev_needed_before))
	  {
	    if (dump_file)
	      fprintf (dump_file, "Capture rotation moved uid=%d to the "
		       "seam after uid=%d target=%s\n",
		       cand_uid, INSN_UID (last), rotation_target_name ());
	    return true;
	  }
	reorder_insns (cand, cand, restore_after);
      }

  if (dump_file)
    fprintf (dump_file, "Capture rotation refused: no independent filler "
	     "reaches the seam of bb %d\n", row.bb->index);
  return false;
}

/* Close a modeled in-row stall by moving one independent row member
   into the gap, like the seam fill but for interior gaps and with the
   wider filler classes.  Runs after the seam and prologue movers.  The
   adjacency accounting is cyclic: a candidate at either end of the row
   also changes the seam adjacency.  */

static bool
rotate_interior_fill (rotation_row const &row,
		      std::vector<basic_block> &visited)
{
  auto const &issued = row.issued;
  unsigned m = issued.size ();

  for (unsigned i = 0; i + 1 < m; ++i)
    {
      rtx_insn *producer = issued[i];
      rtx_insn *consumer = issued[i + 1];
      int s = adjacency_stall (producer, consumer);
      if (s < 0)
	{
	  if (dump_file)
	    fprintf (dump_file, "Capture rotation refused: unaudited result "
		     "latency after uid=%d\n", INSN_UID (producer));
	  continue;
	}
      if (s == 0)
	continue;
      /* Required-NOP sites inside the row are left to the NOP
	 inserter and fill_nop_shadows.  */
      if (get_attr_xtt_delay (producer) == XTT_DELAY_DYNAMIC
	  && delay_nop_needed_p (visited, row.bb, producer,
				 XTT_DELAY_DYNAMIC))
	continue;
      if (dump_file)
	fprintf (dump_file, "Capture rotation: modeled in-row stall after "
		 "uid=%d in bb %d\n", INSN_UID (producer), row.bb->index);

      /* Forward candidates nearest-first, then backward nearest-first.  */
      for (unsigned step = 0; step != m; ++step)
	{
	  unsigned o;
	  if (step <= i && i - step <= i)
	    o = i - step;		/* i, i-1, ..., 0 */
	  else
	    o = i + 1 + (step - i);	/* i+2, i+3, ..., m-1 */
	  if (o == i || o == i + 1 || o >= m)
	    continue;

	  rtx_insn *cand = issued[o];
	  insn_regs cand_regs;
	  bool hidden_free;
	  const char *kind_why;
	  rotation_filler_kind kind
	    = rotation_filler_kind_p (cand, &cand_regs, &hidden_free,
				      &kind_why);
	  if (kind == ROT_FILLER_REFUSED)
	    {
	      if (kind_why && dump_file)
		fprintf (dump_file, "Capture rotation refused: filler "
			 "uid=%d %s\n", INSN_UID (cand), kind_why);
	      continue;
	    }
	  if (kind == ROT_FILLER_RWC_STEP && riscv_tt_opt_replay_hoist)
	    {
	      rvtt_refuse (RVTT_REF_ROW_STEP, dump_file,
			   "Capture rotation refused: row-step "
			   "filler uid=%d deferred to replay capture "
			   "formation\n", INSN_UID (cand));
	      continue;
	    }
	  if (audited_latency (cand) != 0)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler "
			 "uid=%d carries an unaudited or nonzero result "
			 "latency\n", INSN_UID (cand));
	      continue;
	    }
	  /* A STATIC-delay filler would bring its pad into the vacated
		 slot.  */
	  if (get_attr_xtt_delay (cand) == XTT_DELAY_STATIC)
	    continue;

	  insn_regs crossed;
	  rtx_insn *blocker
	    = o < i ? rotation_crossed_segment_kind (NEXT_INSN (cand),
						     producer, kind,
						     hidden_free, &crossed)
		    : rotation_crossed_segment_kind (consumer,
						     PREV_INSN (cand), kind,
						     hidden_free, &crossed);
	  if (blocker)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler "
			 "uid=%d cannot cross uid=%d\n",
			 INSN_UID (cand), INSN_UID (blocker));
	      continue;
	    }
	  if (hard_reg_set_intersect_p (cand_regs.uses, crossed.defs)
	      || hard_reg_set_intersect_p (cand_regs.defs, crossed.uses)
	      || hard_reg_set_intersect_p (cand_regs.defs, crossed.defs))
	    continue;

	  /* Recount the vacated position, cyclically.  */
	  rtx_insn *prev = issued[(o + m - 1) % m];
	  rtx_insn *next = issued[(o + 1) % m];
	  int s_prev_cand = adjacency_stall (prev, cand);
	  int s_cand_next = adjacency_stall (cand, next);
	  int s_prev_next = adjacency_stall (prev, next);
	  int s_p_cand = adjacency_stall (producer, cand);
	  int s_cand_c = adjacency_stall (cand, consumer);
	  if (s_prev_cand < 0 || s_cand_next < 0 || s_prev_next < 0
	      || s_p_cand < 0 || s_cand_c < 0)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: unaudited "
			 "latency at the vacated seam of uid=%d\n",
			 INSN_UID (cand));
	      continue;
	    }
	  if (s_prev_next + s_p_cand + s_cand_c
	      >= s_prev_cand + s_cand_next + s)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: no modeled "
			 "stall decrease rotating uid=%d\n",
			 INSN_UID (cand));
	      continue;
	    }

	  bool prev_needed_before
	    = (get_attr_xtt_delay (prev) == XTT_DELAY_DYNAMIC
	       && delay_nop_needed_p (visited, row.bb, prev,
				      XTT_DELAY_DYNAMIC));
	  rtx_insn *restore_after = PREV_INSN (cand);
	  int cand_uid = INSN_UID (cand);

	  reorder_insns (cand, cand, producer);
	  if (rotation_delay_clean_p (visited, row.bb, producer, cand,
				      prev, prev_needed_before))
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation moved uid=%d into "
			 "the in-row stall after uid=%d target=%s\n",
			 cand_uid, INSN_UID (producer),
			 rotation_target_name ());
	      return true;
	    }
	  reorder_insns (cand, cand, restore_after);
	}
    }
  return false;
}

/* Close a modeled stall (in the row or at the seam) by moving an
   invariant-input member forward past its own consumers, with a copy in
   the preheader for the run's first row.  */

static bool
rotate_prologue_fill (rotation_row const &row, basic_block preheader,
		      std::vector<basic_block> &visited)
{
  auto const &issued = row.issued;
  unsigned m = issued.size ();

  /* Registers written anywhere in the row, for the invariance and
     single-writer checks.  */
  insn_regs rowwide;
  CLEAR_HARD_REG_SET (rowwide.uses);
  CLEAR_HARD_REG_SET (rowwide.defs);
  rtx_insn *walk;
  FOR_BB_INSNS (row.bb, walk)
    if (NONDEBUG_INSN_P (walk))
      {
	insn_regs w;
	sfpu_reg_refs (walk, &w);
	rowwide.uses |= w.uses;
	rowwide.defs |= w.defs;
      }

  /* Lane-predicated fillers are allowed only if no issued row member
     writes CC.  */
  bool row_cc_clean = true;
  for (rtx_insn *member : issued)
    if (!bare_lreg_copy_p (member))
      {
	xtt_effect_set me = rvtt_insn_effects (member);
	if (me.opaque || me.cc_write)
	  row_cc_clean = false;
      }

  /* Gap index i: in-row adjacencies (issued[i], issued[i+1]) for
     i < m - 1, then the seam (issued[m-1], issued[0]) at i == m - 1.  */
  for (unsigned i = 0; i != m; ++i)
    {
      rtx_insn *producer = issued[i];
      rtx_insn *consumer = issued[(i + 1) % m];
      bool seam = i == m - 1;

      int s = adjacency_stall (producer, consumer);
      if (s < 0)
	{
	  if (dump_file)
	    fprintf (dump_file, "Capture rotation refused: unaudited result "
		     "latency after uid=%d\n", INSN_UID (producer));
	  continue;
	}
      if (s == 0)
	continue;
      /* Required-NOP sites inside the row are left to the NOP inserter
	 and fill_nop_shadows; the seam has no other handler.  */
      if (!seam
	  && get_attr_xtt_delay (producer) == XTT_DELAY_DYNAMIC
	  && delay_nop_needed_p (visited, row.bb, producer,
				 XTT_DELAY_DYNAMIC))
	continue;
      if (dump_file)
	fprintf (dump_file, "Capture rotation: modeled stall after uid=%d "
		 "in bb %d\n", INSN_UID (producer), row.bb->index);

      /* Forward moves only: candidates strictly before the gap, or for
	 the seam, any member after the first word (the consumer).  */
      unsigned limit = seam ? m - 1 : i;
      for (unsigned o = limit; o-- != (seam ? 1 : 0);)
	{
	  rtx_insn *cand = issued[o];
	  bool hidden_free = bare_lreg_copy_p (cand);
	  xtt_effect_set eff = rvtt_insn_effects (cand);
	  if (!hidden_free
	      && (eff.opaque || eff.cc_write
		  || eff.config_dests_written || eff.config_dests_read
		  || eff.rwc.kind != xtt_rwc_effect_t::NONE
		  || eff.dst_mem_read || eff.dst_mem_write
		  || contains_mem_rtx_p (PATTERN (cand))))
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "touches Dst, RWC, CC, or configuration state\n",
			 INSN_UID (cand));
	      continue;
	    }
	  if (!hidden_free && eff.cc_read && !row_cc_clean)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "is lane-predicated in a row with unproven CC "
			 "state\n", INSN_UID (cand));
	      continue;
	    }
	  if (audited_latency (cand) != 0)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "carries an unaudited or nonzero result latency\n",
			 INSN_UID (cand));
	      continue;
	    }
	  if (get_attr_xtt_delay (cand) == XTT_DELAY_STATIC)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "carries a static delay contract\n", INSN_UID (cand));
	      continue;
	    }
	  /* The prologue copy must be a single SET: a copied hard-register
	     clobber could hit live preheader state.  */
	  if (GET_CODE (PATTERN (cand)) != SET || !single_set (cand))
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "is not a plain single-set word\n", INSN_UID (cand));
	      continue;
	    }
	  insn_regs cand_regs;
	  if (!collect_sfpu_regs (cand, &cand_regs))
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "touches scalar state\n", INSN_UID (cand));
	      continue;
	    }
	  if (hard_reg_set_intersect_p (cand_regs.uses, rowwide.defs))
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "reads registers written inside the row\n",
			 INSN_UID (cand));
	      continue;
	    }

	  /* Single writer, and no reader before the filler (it would see
	     the prologue's value).  */
	  bool sole_writer = true;
	  bool carried_read = false;
	  FOR_BB_INSNS (row.bb, walk)
	    {
	      if (walk == cand || !NONDEBUG_INSN_P (walk))
		continue;
	      insn_regs w;
	      sfpu_reg_refs (walk, &w);
	      if (hard_reg_set_intersect_p (w.defs, cand_regs.defs))
		{
		  sole_writer = false;
		  break;
		}
	    }
	  for (walk = BB_HEAD (row.bb); walk != cand;
	       walk = NEXT_INSN (walk))
	    {
	      if (!NONDEBUG_INSN_P (walk))
		continue;
	      insn_regs w;
	      sfpu_reg_refs (walk, &w);
	      if (hard_reg_set_intersect_p (w.uses, cand_regs.defs))
		{
		  carried_read = true;
		  break;
		}
	    }
	  if (!sole_writer)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "writes a register another row member also "
			 "writes\n", INSN_UID (cand));
	      continue;
	    }
	  if (carried_read)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "carries a live value across the row boundary\n",
			 INSN_UID (cand));
	      continue;
	    }
	  bool live_in = false;
	  for (unsigned r = 0; r != FIRST_PSEUDO_REGISTER; ++r)
	    if (TEST_HARD_REG_BIT (cand_regs.defs, r)
		&& bitmap_bit_p (DF_LR_IN (row.bb), r))
	      live_in = true;
	  if (live_in)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "destination is live into the row\n",
			 INSN_UID (cand));
	      continue;
	    }

	  insn_regs crossed;
	  rtx_insn *blocker
	    = rotation_crossed_segment (NEXT_INSN (cand), producer,
					hidden_free, &crossed);
	  if (blocker)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "cannot cross uid=%d\n",
			 INSN_UID (cand), INSN_UID (blocker));
	      continue;
	    }

	  rtx_insn *prev = issued[o ? o - 1 : m - 1];
	  rtx_insn *next = issued[o + 1];
	  int s_prev_cand = adjacency_stall (prev, cand);
	  int s_cand_next = adjacency_stall (cand, next);
	  int s_prev_next = adjacency_stall (prev, next);
	  int s_p_cand = adjacency_stall (producer, cand);
	  int s_cand_c = adjacency_stall (cand, consumer);
	  if (s_prev_cand < 0 || s_cand_next < 0 || s_prev_next < 0
	      || s_p_cand < 0 || s_cand_c < 0)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: unaudited "
			 "latency at the vacated seam of uid=%d\n",
			 INSN_UID (cand));
	      continue;
	    }
	  if (s_prev_next + s_p_cand + s_cand_c
	      >= s_prev_cand + s_cand_next + s)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: no modeled "
			 "stall decrease rotating uid=%d\n", INSN_UID (cand));
	      continue;
	    }
	  if (!preheader)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: no dedicated "
			 "preheader for the prologue of bb %d\n",
			 row.bb->index);
	      continue;
	    }

	  bool prev_needed_before
	    = (get_attr_xtt_delay (prev) == XTT_DELAY_DYNAMIC
	       && delay_nop_needed_p (visited, row.bb, prev,
				      XTT_DELAY_DYNAMIC));
	  rtx_insn *restore_after = PREV_INSN (cand);
	  int cand_uid = INSN_UID (cand);

	  reorder_insns (cand, cand, producer);
	  if (!rotation_delay_clean_p (visited, row.bb, producer, cand,
				       prev, prev_needed_before))
	    {
	      reorder_insns (cand, cand, restore_after);
	      continue;
	    }

	  rtx_insn *end = BB_END (preheader);
	  rtx_insn *pro = JUMP_P (end)
	    ? emit_insn_before (copy_rtx (PATTERN (cand)), end)
	    : emit_insn_after (copy_rtx (PATTERN (cand)), end);
	  INSN_LOCATION (pro) = INSN_LOCATION (cand);
	  df_insn_rescan (pro);
	  if (dump_file)
	    fprintf (dump_file, "Capture rotation moved uid=%d into the "
		     "stall after uid=%d with prologue uid=%d target=%s\n",
		     cand_uid, INSN_UID (producer), INSN_UID (pro),
		     rotation_target_name ());
	  return true;
	}
    }
  return false;
}

/* Capture-rotation driver.  Targets without latency data are refused.
   For every block of FN that is a capturable self-loop row, apply the
   three movers (seam, prologue, interior; each committed move restarts
   them) until none applies or the row no longer qualifies.  */

static void
rotate_capture_rows (function *fn)
{
  if (!TARGET_XTT_TENSIX_BH && !TARGET_XTT_TENSIX_WH)
    {
      if (dump_file)
	fprintf (dump_file, "Capture rotation refused: no audited latency "
		 "facts for this target\n");
      return;
    }

  df_analyze ();

  std::vector<basic_block> visited;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rotation_row row;
      const char *reason;
      if (!rotation_row_p (bb, &row, &reason))
	{
	  if (reason && dump_file)
	    rvtt_refuse_by_name (reason, dump_file,
				 "Capture rotation refused: %s in bb %d\n",
				 reason, bb->index);
	  continue;
	}
      basic_block preheader = rotation_dedicated_preheader (bb);

      visited.reserve (n_basic_blocks_for_fn (fn));
      while (rotate_seam_fill (row, visited)
	     || rotate_prologue_fill (row, preheader, visited)
	     || rotate_interior_fill (row, visited))
	if (!rotation_row_p (bb, &row, &reason))
	  break;
    }
}

// Perform instruction scheduling. We conditionally insert a nop after
// instructions.  Also handle sfpstore-sfpload latency on QSR

static void
transform (function *fn)
{
  std::vector<basic_block> visited;

  basic_block bb;
  FOR_ALL_BB_FN (bb, fn)
    bb->flags &= ~BB_VISITED;

  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;

      FOR_BB_INSNS (bb, insn)
	{
	  if (GET_CODE (insn) != INSN)
	    continue;

	  if (recog_memoized (insn) < 0)
	    continue;

	  if (get_attr_type (insn) != TYPE_TENSIX)
	    continue;

	  enum xtt_delay delay = get_attr_xtt_delay (insn);
	  if (delay == XTT_DELAY_NONE)
	    {
	      if (INSN_CODE (insn) == CODE_FOR_rvtt_sfpload_lv_int
		  && TARGET_XTT_TENSIX_QSR)
		{
		  // Do we need a nop before this load?  Gimple schedule pass
		  // will have handled when we can propagate the stored value.
		  auto src = SET_SRC (XVECEXP (PATTERN (insn), 0, 0));
		  if (const0_rtx == XVECEXP (src, 0, rvtt_synth::IX_mem))
		    {
		      auto addr = XVECEXP (src, 0, rvtt_synth::IX_insn);
		      handle_qsr_load_latency (visited, bb, insn, addr);
		      for (auto *bb : visited)
			bb->flags &= ~BB_VISITED;
		      visited.clear ();
		    }
		}

	      continue;
	    }

	  visited.reserve (n_basic_blocks_for_fn (fn));
	  bool insert = delay_nop_needed_p (visited, bb, insn, delay);

	  if (insert)
	    for (unsigned nops = rvtt_delay_bubbles (insn); nops; --nops)
	      emit_insn_after (gen_rvtt_sfpnop (), insn);
	  if (dump_file)
	    {
	      fprintf (dump_file, "%snserting %s nop after ",
		       insert ? "I" : "Not i",
		       delay == XTT_DELAY_STATIC ? "static" : "dynamic");
	      dump_insn_slim (dump_file, insn);
	      fprintf (dump_file, "\n");
	    }
       }
    }
}

namespace {

const pass_data pass_data_rvtt_schedule =
{
  RTL_PASS, /* type */
  "rvtt_schedule", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_schedule : public rtl_opt_pass
{
public:
  pass_rvtt_schedule (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_schedule, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }

  virtual unsigned execute (function *fn) override
  {
    if (riscv_tt_opt_crossrow_pairing)
      /* Run before the region schedulers, which may still improve the
	 pure spans of a doubled row.  */
      crossrow_pair_rows (fn);
    if (riscv_tt_opt_list_schedule || riscv_tt_opt_round_interleave
	|| riscv_tt_opt_cyclic_region_schedule || riscv_tt_opt_ims)
      /* round-interleave enables only the cyclic self-loop and
	 isomorphic-pair paths, cyclic-region-schedule only the
	 multi-region self-loop path, and ims only its candidate orders
	 on those cyclic paths; single straight-line regions need
	 list-schedule.  */
      list_schedule_regions (fn);
    if (riscv_tt_opt_latency_schedule)
      {
	fill_latency_bubbles (fn);
	fill_nop_shadows (fn);
      }
    if (riscv_tt_opt_interlock_schedule)
      fill_interlock_shadows (fn);
    if (riscv_tt_opt_capture_rotation)
      rotate_capture_rows (fn);
    transform (fn);
    return 0;
  }
}; /* class pass_rvtt_schedule */

} /* anon namespace */

/* Create the Tensix scheduling pass.  */

rtl_opt_pass *
make_pass_rvtt_schedule (gcc::context *ctxt)
{
  return new pass_rvtt_schedule (ctxt);
}

/* Drain elision at SFPLOADMACRO run boundaries
   (-mtt-tensix-optimize-drain-schedule).

   The macro planner emits core_drain_slots SFPNOPs after every formed run:
   the greatest event writeback distance past the run's last issue slot,
   derived from the descriptor's SequenceBits delays
   (rvtt-macro-sched-core.h).  When a region is split into several runs,
   the drain is only needed before the first follower access that
   actually conflicts.  The SFPLOADMACRO functional specification gives:

     State latched at launch (safe to change while events are in flight):
       L1  the store event's Dst row (dst_rwc + imm10 + DEST_TARGET);
       L2  the store format (Misc/launch Mod0);
       L3  the Dst layout the format decode depends on.
     State read at execution (changes inside the horizon race):
       E1  the lane predicate: a CC write changes which lanes an in-flight
	   event touches;
       E2  LREG contents read or written by staged events;
       E3  the LoadMacroConfig state (templates, sequences, misc);
       E4  Dst rows written by an in-flight store.
     Timing:
       H1  event writeback slot = carrier issue slot + programmed delay,
	   so a run's last writeback is at most drain_slots past its final
	   issue slot;
       H2  at most one instruction issues per cycle, so a follower word's
	   stream position is a lower bound on its cycle distance from the
	   boundary; stalls only move accesses later.

   The drain after run K may be elided when (a) everything between runs K
   and K+1 is a recorded pure-RWC run separator (launch-latched state
   only) whose issued words survive to the final stream (FACE-class typed
   advances and raw SETRWC-class words do; INC-class TTINCRWC may be
   absorbed by the Dst auto-increment fold and earns no credit), (b) every
   event of run K+1 that touches architectural state executes strictly
   after the last pending writeback, and (c) the follower words examined
   cover the whole horizon.  The final run's drain is never elided here;
   that is the caller's responsibility.  */

static bool
drain_refuse (FILE *dump, const char *name, rtx_insn *insn)
{
  if (dump)
    {
      rvtt_refuse_by_name (name, dump,
			   "Macro-planner drain-refusal: %s", name);
      if (insn)
	fprintf (dump, " (insn %d)", INSN_UID (insn));
      fprintf (dump, "\n");
    }
  return false;
}

/* Region members are deleted or re-emitted inside the formed
   calendar, so the boundary walk skips them.  */

static bool
drain_region_member_p (const macro_region &region, rtx_insn *insn)
{
  for (const macro_row &row : region.rows)
    {
      if (insn == row.separator || insn == row.enable)
	return true;
      for (rtx_insn *member : row.insns)
	if (insn == member)
	  return true;
    }
  return false;
}

/* Return true if INSN is one of REGION's recorded run separators.  */

static bool
drain_run_separator_p (const macro_region &region, rtx_insn *insn)
{
  for (rtx_insn *sep : region.run_separators)
    if (sep == insn)
      return true;
  return false;
}

/* Return the refusal name for a follower-event conflict, by effect
   class (E4, E1, E3, E2 in order of specificity).  */

static const char *
drain_conflict_name (const xtt_effect_set &e)
{
  if (e.dst_mem_read || e.dst_mem_write)
    return "drain-dst-raw";
  if (e.cc_read || e.cc_write)
    return "drain-cc-live";
  if (e.config_dests_written || e.config_dests_read || e.addr_mod_slot_write)
    return "drain-config-overlap";
  return "drain-lreg-overlap";
}

/* Pending-event horizon of one emitted run, used by both the
   intra-region boundary check and the loop-backedge check below.  All
   fields come from the descriptor's SequenceBits delays and the schedule's
   slot assignment.  */

struct drain_horizon
{
  int max_dist;			/* == desc.drain_slots, checked  */
  unsigned words_per_row;
  int carrier_pos[8];
  auto_vec<int> word_pos;	/* per schedule event: row position    */
  struct { unsigned kind; unsigned delay; } events[8][4];
  int n_events[8];
};

/* Decode the run horizon from SCHEDULE and DESC into *H.  Word positions
   follow emit_planner_run's emission order (issue slots ascending).  Event
   timing is decoded from the descriptor's sequence words in the
   SequenceBits format (rvtt-macro-tables.h: byte i programs sub-unit i;
   case = bits 2:0, and the event executes at issue + 1 + delay (bits
   5:3); see rvtt-macro-derive-core.h).  The schedule may record
   DELAY_UNKNOWN for whole-word programs, but the words themselves carry
   the delays.  Case 1 is undefined; SKIP/NOP bytes stage no event.  The
   decoded horizon is checked against the descriptor's drain_slots, and a
   mismatch refuses.  */

/* REQUIRE_EXACT selects the check.  The intra-region boundary check
   requires the decoded horizon to equal the descriptor's drain_slots.
   The loop-backedge check accepts a larger emitted drain (the compact CC
   program records 3 where the SequenceBits only pend 1) and uses the
   decoded horizon, refusing if it exceeds the emitted drain.  */

static bool
drain_decode_horizon (const macro_schedule &schedule,
		      const macro_descriptor &desc,
		      drain_horizon *h, FILE *dump, bool require_exact)
{
  h->max_dist = desc.drain_slots;

  for (int m = 0; m != 8; ++m)
    h->carrier_pos[m] = -1;
  h->word_pos.safe_grow_cleared (schedule.events.length ());
  for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
    h->word_pos[ix] = -1;
  h->words_per_row = 0;
  for (int slot = 0; slot != schedule.ii; ++slot)
    for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
      {
	const macro_event &ev = schedule.events[ix];
	if (!ev.issues_word || ev.slot != slot)
	  continue;
	h->word_pos[ix] = h->words_per_row;
	if (ev.is_carrier && ev.macro_index < 8)
	  h->carrier_pos[ev.macro_index] = h->words_per_row;
	++h->words_per_row;
      }
  if (h->words_per_row == 0)
    return drain_refuse (dump, "drain-follower-opaque", nullptr);

  for (int m = 0; m != 8; ++m)
    h->n_events[m] = 0;
  for (unsigned m = 0; m != desc.n_seq && m != 8; ++m)
    {
      uint8_t bytes[4];
      rvtt_macro::decompose_sequence_word (desc.seq[m], bytes);
      for (int i = 0; i != 4; ++i)
	{
	  unsigned kind, delay;
	  bool vd16, route_vb;
	  if (!rvtt_macro::decode_sequence_bits (bytes[i], &kind, &delay,
						 &vd16, &route_vb))
	    return drain_refuse (dump, "drain-delay-unproven", nullptr);
	  if (kind == rvtt_macro::SEQ_CASE_SKIP
	      || kind == rvtt_macro::SEQ_CASE_NOP)
	    continue;
	  h->events[m][h->n_events[m]].kind = kind;
	  h->events[m][h->n_events[m]].delay = delay;
	  ++h->n_events[m];
	}
    }

  /* Every sequence word's events need a known carrier; a macro without
     one leaves events unaccounted.  */
  for (unsigned m = 0; m != desc.n_seq && m != 8; ++m)
    if (h->n_events[m] > 0 && h->carrier_pos[m] < 0)
      return drain_refuse (dump, "drain-delay-unproven", nullptr);

  /* Pending horizon: the greatest event execution distance past the
     run's last issue slot, over the events of the trailing rows (event
     execution = carrier position + 1 + delay; rows are words_per_row
     issue slots apart).  */
  int last_issue = (int) h->words_per_row - 1;
  int max_pending = 0;
  for (int j = 0;
       j * (int) h->words_per_row <= (int) rvtt_macro::SEQ_MAX_DELAY + 1;
       ++j)
    for (unsigned m = 0; m != desc.n_seq && m != 8; ++m)
      {
	if (h->carrier_pos[m] < 0)
	  continue;
	for (int e = 0; e != h->n_events[m]; ++e)
	  {
	    int dist = h->carrier_pos[m] + 1 + (int) h->events[m][e].delay
	      - last_issue - j * (int) h->words_per_row;
	    if (dist > max_pending)
	      max_pending = dist;
	  }
      }
  if (require_exact ? max_pending != h->max_dist
		    : max_pending > h->max_dist)
    return drain_refuse (dump, "drain-delay-unproven", nullptr);
  if (!require_exact)
    h->max_dist = max_pending;
  return true;
}

/* Return true if every architectural access of the follower rows
   [ROW_BEGIN, ROW_END) is ordered after every pending event of horizon H,
   given SEP_CREDIT separator words already issued before the first row
   (H1, H2).  A follower word at run position P issues no earlier than
   boundary + sep_credit + 1 + P; its launched events execute at
   issue + 1 + delay; an explicit word's own access, and a carrier's own
   front-end VD write, are counted at issue.  Following rvtt-macro-tables.h,
   a staged event retiring at cycle X retires before a front-end
   instruction issuing at X executes, so a front-end access at the last
   retirement cycle is ordered; two staged events at the same cycle still
   race, so launched follower events need a strict inequality.  The
   follower words examined must cover the whole horizon.  */

static bool
drain_follower_rows_ok (const macro_region &region,
			const macro_schedule &schedule,
			const macro_descriptor &desc,
			const drain_horizon &h,
			unsigned row_begin, unsigned row_end,
			unsigned sep_credit, FILE *dump)
{
  int max_dist = h.max_dist;
  unsigned base = sep_credit;	/* follower words issued before this row */
  for (unsigned r = row_begin; r != row_end; ++r)
    {
      if ((int) base + 1 > max_dist)
	break;			/* every later access is late enough */
      if (region.rows[r].insns.length () != schedule.events.length ())
	return drain_refuse (dump, "drain-follower-opaque", nullptr);
      /* The launch word's own front-end VD write.  Issuing at the last
	 retirement cycle is fine, but an earlier issue writes VD while
	 the horizon's events (consumers of the same descriptor's
	 previous rows) are still in flight.  The write is safe only
	 under the protections of macro_launch_spec (rvtt-macro-desc.h):
	 VD alternation (adjacent rows use different registers), a
	 store-only sacrificial VD (written, never read), or the
	 CC-template model's next-row guarantees (macro_cc_model).  A
	 fixed-VD value carrier has none of these and is refused, as is
	 a carrier with no launch spec.  */
      for (unsigned m = 0; m != desc.n_seq && m != 8; ++m)
	{
	  if (h.carrier_pos[m] < 0)
	    continue;
	  int issue = (int) base + 1 + h.carrier_pos[m];
	  if (issue >= max_dist)
	    continue;		/* front-end: equality allowed */
	  if (desc.cc.active)
	    continue;		/* covered by the CC-template model */
	  const macro_launch_spec *spec = nullptr;
	  for (const macro_launch_spec &l : desc.launches)
	    if (l.macro_index == m)
	      {
		spec = &l;
		break;
	      }
	  if (!spec)
	    return drain_refuse (dump, "drain-follower-opaque", nullptr);
	  if (!spec->vd_alternates && !spec->is_store_only)
	    return drain_refuse (dump, "drain-follower-vd-write", nullptr);
	}
      /* Launched events, per carrier word.  */
      for (unsigned m = 0; m != desc.n_seq && m != 8; ++m)
	{
	  if (h.carrier_pos[m] < 0)
	    continue;
	  for (int e = 0; e != h.n_events[m]; ++e)
	    {
	      int exec = (int) base + 1 + h.carrier_pos[m] + 1
		+ (int) h.events[m][e].delay;
	      if (exec > max_dist)
		continue;	/* after the last writeback */
	      return drain_refuse
		(dump, h.events[m][e].kind == rvtt_macro::SEQ_CASE_STORE
		 ? "drain-dst-raw" : "drain-lreg-overlap", nullptr);
	    }
	}
      /* Explicit words, at issue.  */
      for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
	{
	  const macro_event &ev = schedule.events[ix];
	  if (ev.realization != macro_event::EXPLICIT_INSN
	      || h.word_pos[ix] < 0)
	    continue;
	  int access_lb = (int) base + 1 + h.word_pos[ix];
	  /* Front-end access: equality with the last pending retirement is
	     allowed.  */
	  if (access_lb >= max_dist)
	    continue;
	  xtt_effect_set e = rvtt_insn_effects (region.rows[r].insns[ix]);
	  return drain_refuse (dump, drain_conflict_name (e),
			       region.rows[r].insns[ix]);
	}
      base += h.words_per_row;
      if (desc.keep_separator && region.rows[r].separator)
	base += 1;		/* pure-RWC word re-emitted unchanged */
    }

  /* The follower words examined must cover the whole horizon.  */
  if ((int) base < max_dist)
    return drain_refuse (dump, "drain-horizon-spill", nullptr);
  return true;
}

/* Return true if the drain after REGION's run ending at row END may be
   elided (see the comment above).  BEGIN is unused; the follower run is
   rows [END, NEXT_END).  SCHEDULE and DESC are the row schedule and its
   descriptor.  Checks that (a) only recorded run separators lie between
   the runs, crediting pure-RWC words that are never absorbed, (b) the
   decoded horizon equals the emitted drain, and (c) every follower
   access is after that horizon.  Refusals are reported to DUMP.  */

bool
rvtt_macro_drain_boundary_elidable (const macro_region &region,
				    const macro_schedule &schedule,
				    const macro_descriptor &desc,
				    unsigned begin, unsigned end,
				    unsigned next_end, FILE *dump)
{
  (void) begin;
  int max_dist = desc.drain_slots;
  if (max_dist < 0)
    /* Formation refuses unknown delays before emission; check again
       here anyway (drain-delay-unproven is the CORE_DELAY_UNKNOWN
       case).  */
    return drain_refuse (dump, "drain-delay-unproven", nullptr);
  if (max_dist == 0)
    return true;		/* Nothing to elide.  */
  if (end >= next_end || next_end > region.rows.length ())
    return drain_refuse (dump, "drain-follower-opaque", nullptr);

  /* (a) Only separators, with launch-latched effects, between runs.  */
  const macro_row &last_row = region.rows[end - 1];
  rtx_insn *from = last_row.separator
    ? last_row.separator : last_row.insns[last_row.insns.length () - 1];
  const macro_row &next_row0 = region.rows[end];
  rtx_insn *to = next_row0.enable ? next_row0.enable : next_row0.insns[0];

  unsigned sep_credit = 0;
  for (rtx_insn *insn = NEXT_INSN (from); insn != to;
       insn = insn ? NEXT_INSN (insn) : nullptr)
    {
      if (!insn)
	return drain_refuse (dump, "drain-follower-opaque", nullptr);
      if (!NONDEBUG_INSN_P (insn))
	continue;
      rtx pat = PATTERN (insn);
      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	continue;
      if (drain_region_member_p (region, insn))
	continue;
      if (!drain_run_separator_p (region, insn))
	return drain_refuse (dump, "drain-follower-opaque", insn);
      xtt_effect_set e = rvtt_insn_effects (insn);
      if (e.opaque || e.lreg_read || e.lreg_write || e.cc_read || e.cc_write
	  || e.config_dests_written || e.config_dests_read
	  || e.addr_mod_slot_write || e.dst_mem_read || e.dst_mem_write)
	return drain_refuse (dump, "drain-follower-opaque", insn);
      switch (e.rwc.kind)
	{
	case xtt_rwc_effect_t::SET:
	case xtt_rwc_effect_t::FACE:
	  /* Launch-latched only (L1) and never absorbed by a later pass
	     (AIC_RWC_STEP), so its words keep their stream slots and
	     earn credit.  A raw .ttinsn word is one word
	     (rvtt_raw_ttinsn_word); a typed pattern's word count is its
	     length in 4-byte Tensix words.  */
	  if (asm_noperands (pat) >= 0)
	    sep_credit += 1;
	  else
	    sep_credit += get_attr_length (insn) / 4;
	  break;
	case xtt_rwc_effect_t::INC:
	  /* Launch-latched (L1), but the Dst auto-increment fold may
	     absorb it (AIC_INCRWC), so it earns no slot credit.  */
	  break;
	default:
	  return drain_refuse (dump, "drain-follower-opaque", insn);
	}
    }

  drain_horizon h;
  if (!drain_decode_horizon (schedule, desc, &h, dump, /*require_exact=*/true))
    return false;
  if (dump)
    fprintf (dump, "Macro-planner drain-boundary: drain=%d"
	     " separator-credit=%u words-per-row=%u\n",
	     h.max_dist, sep_credit, h.words_per_row);

  /* (b) and (c): the next run's accesses and horizon coverage.  */
  if (!drain_follower_rows_ok (region, schedule, desc, h, end, next_end,
			       sep_credit, dump))
    return false;

  if (dump)
    fprintf (dump, "Macro-planner drain-schedule: run-boundary drain"
	     " elided (drain=%d separator-credit=%u)\n",
	     h.max_dist, sep_credit);
  return true;
}

/* Loop-backedge drain elision.

   The final run of a loop-body region ends at the latch, so its drain
   executes on every trip although it is only needed once on loop exit.
   The follower stream across the backedge is the body tail after the
   final run, anything before the region at the head of the body, and the
   region's own first run in the next iteration.  It is checked with the
   same slot arithmetic (drain_decode_horizon, drain_follower_rows_ok)
   after classifying every instruction in between:

     - pure-RWC words that are never absorbed (SET/FACE class,
       AIC_RWC_STEP) earn slot credit, absorbable INC words do not, as
       for run separators;
     - scalar instructions with no call, asm or memory store earn no
       credit (a scalar can only affect Tensix state by pushing a word
       through the instruction FIFO, which is a store); they can only
       delay follower issue;
     - anything else is refused.

   The caller emits the full drain on the loop's exit path.  The first
   trip has no pending events at loop entry.  Later replay/MOP delivery of
   the body can only add issue slots before the follower words, which is
   the safe direction (H2).  */

static void
drain_note_mem_store (rtx x, const_rtx, void *data)
{
  if (MEM_P (x))
    *(bool *) data = true;
}

/* Classify one instruction INSN between the final run and the
   backedge.  Return true if it cannot conflict with pending events,
   adding its slot credit to *CREDIT; otherwise return false with the
   refusal name in *WHY.  */

static bool
drain_stream_insn_neutral (rtx_insn *insn, unsigned *credit,
			   const char **why)
{
  *why = "drain-follower-opaque";
  rtx pat = PATTERN (insn);
  if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
    return true;
  if (CALL_P (insn))
    return false;
  xtt_effect_set e = rvtt_insn_effects (insn);
  if (!e.opaque)
    {
      if (e.lreg_read || e.lreg_write || e.cc_read || e.cc_write
	  || e.config_dests_written || e.config_dests_read
	  || e.addr_mod_slot_write || e.dst_mem_read || e.dst_mem_write)
	{
	  /* An effect conflict reports its class (E1-E4).  */
	  *why = drain_conflict_name (e);
	  return false;
	}
      switch (e.rwc.kind)
	{
	case xtt_rwc_effect_t::SET:
	case xtt_rwc_effect_t::FACE:
	  /* Never-absorbed launch-latched words keep their stream slots
	     (AIC_RWC_STEP) and earn credit, sized as in the
	     intra-region walk.  */
	  if (asm_noperands (pat) >= 0)
	    *credit += 1;
	  else
	    *credit += get_attr_length (insn) / 4;
	  return true;
	case xtt_rwc_effect_t::INC:
	  /* Absorbable (AIC_INCRWC): no conflict, no credit.  */
	  return true;
	case xtt_rwc_effect_t::NONE:
	  /* Effect-free Tensix instruction: no conflict; it may not
	     survive in the final stream, so no credit.  */
	  return true;
	default:
	  return false;
	}
    }
  /* Tensix instruction with unknown effects or unknown raw word:
	     refuse.  */
  if (asm_noperands (pat) >= 0)
    return false;
  if (recog_memoized (insn) >= 0 && get_attr_type (insn) == TYPE_TENSIX)
    return false;
  /* A scalar instruction can only affect Tensix state through a
     memory store; refuse stores, accept other scalars without
     credit.  */
  bool stores_mem = false;
  note_stores (insn, drain_note_mem_store, &stores_mem);
  if (stores_mem)
    return false;
  return true;
}

/* Return true if the drain after REGION's final run may be elided across
   the backedge of its single-block loop body (see the comment above).
   SCHEDULE and DESC are the row schedule and its descriptor;
   FIRST_RUN_END bounds the rows of the region's first run, which follow
   in the next iteration.  The body tail after the final run and anything
   before the region at the body head are classified for slot credit, and
   the decoded pending horizon (at most the emitted drain) must clear the
   first-run rows.  Refusals are reported to DUMP.

   The loop exits through the latch, so the exit path runs the same tail;
   once its credited words cover the horizon, no event is still in flight
   when the code after the loop issues.  If EXIT_DRAIN is given, it
   receives the part of the horizon not covered by the tail, which the
   exit path must still drain.  */

bool
rvtt_macro_drain_backedge_elidable (const macro_region &region,
				    const macro_schedule &schedule,
				    const macro_descriptor &desc,
				    unsigned first_run_end, FILE *dump)
{
  if (!region.loop_body || !region.bb)
    return drain_refuse (dump, "drain-follower-opaque", nullptr);
  int max_dist = desc.drain_slots;
  if (max_dist < 0)
    return drain_refuse (dump, "drain-delay-unproven", nullptr);
  if (max_dist == 0)
    return true;
  if (first_run_end == 0 || first_run_end > region.rows.length ())
    return drain_refuse (dump, "drain-follower-opaque", nullptr);

  const char *why = nullptr;
  unsigned credit = 0;

  /* Tail walk: from the final run's end to the end of the loop body,
     including the latch branch.  */
  const macro_row &last_row = region.rows[region.rows.length () - 1];
  rtx_insn *from = last_row.separator
    ? last_row.separator : last_row.insns[last_row.insns.length () - 1];
  for (rtx_insn *insn = NEXT_INSN (from);
       insn && BLOCK_FOR_INSN (insn) == region.bb; insn = NEXT_INSN (insn))
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (drain_region_member_p (region, insn))
	continue;
      /* A pure-RWC run separator left in the tail is classified
	 like any other insn and earns its slot credit.  */
      if (!drain_stream_insn_neutral (insn, &credit, &why))
	return drain_refuse (dump, why, insn);
    }

  /* Head walk: anything before the region at the head of the body
     (executed after the backedge, before the region).  */
  rtx_insn *anchor = region.rows[0].enable
    ? region.rows[0].enable : region.rows[0].insns[0];
  for (rtx_insn *insn = BB_HEAD (region.bb); insn && insn != anchor;
       insn = NEXT_INSN (insn))
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (drain_region_member_p (region, insn))
	continue;
      if (!drain_stream_insn_neutral (insn, &credit, &why))
	return drain_refuse (dump, why, insn);
    }

  drain_horizon h;
  if (!drain_decode_horizon (schedule, desc, &h, dump,
			     /*require_exact=*/false))
    return false;
  if (dump)
    fprintf (dump, "Macro-planner drain-backedge: drain=%d pending=%d"
	     " stream-credit=%u words-per-row=%u\n",
	     desc.drain_slots, h.max_dist, credit, h.words_per_row);

  if (!drain_follower_rows_ok (region, schedule, desc, h, 0, first_run_end,
			       credit, dump))
    return false;

  if (dump)
    fprintf (dump, "Macro-planner drain-schedule: loop-backedge drain"
	     " elided (drain=%d stream-credit=%u)\n",
	     h.max_dist, credit);
  return true;
}


/* Inter-row drain tuning (-mtt-tensix-optimize-window-pairing).

   The default rule places the full drain between consecutive rows
   whenever a launch is a fixed-VD value carrier, without looking at
   registers.  This service derives the minimal inter-row drain from the
   model used above (L1-L3, E1-E4, H1-H2), made exact for registers, Dst
   rows and sub-units:

     - The pending horizon is decoded from the descriptor's SequenceBits,
       as in drain_decode_horizon, and must equal the emitted drain.  Each
       decoded event is mapped back to its originating row instruction by
       the schedule's (macro, realized sub-unit) key; an unaccounted or
       ambiguous event refuses.
     - Each staged event's footprint is its origin instruction's effect
       set, adjusted by the SFPLOADMACRO overrides in its own SequenceBits
       byte (the launch VD is read; the result register is the launch VD
       or LReg[16] according to the VD16 bit; the store's value register
       follows the store override cases), plus the template's hidden LREG
       writes (SFPLOADMACRO.md: Insn.VB/VC/VD overrides; store VD cases
       0x40/0x80).
     - A follower access after every pending writeback needs no further
       check (front-end accesses may be equal under retire-before-issue;
       staged events need a strict inequality).  An access inside the
       horizon is allowed only if its footprint is disjoint from every
       pending event: LREG read/write overlap, CC (either side writing),
       any configuration write and Dst overlap all refuse, and two staged
       events in one cycle also refuse on the same sub-unit (as
       core_check_subunit_occupancy does within a row; SFPLOADMACRO.md
       allows the four sub-unit columns to run concurrently).
     - Dst overlap uses the physical-row model of access_rows
       (gimple-rvtt-transp-involution.cc): an access at constant address A
       touches lane rows [A & ~3, (A & ~3) + 3], mapped through
       dst32b_adjust_row for the 32-bit format class, and the union for
       configuration-resolved classes.  Overlapping row sets are still
       disjoint when the two accesses select opposite column parities
       (Column = (Lane & 7) * 2 + ((Addr & 2) || DEST_{RD,WR}_COL_EXCHANGE),
       SFPLOAD.md, SFPSTORE.md; the column is preserved into DstBits by
       both views, Dst.md), provided the column-exchange LaneConfig bits
       are at their default.  Any function-local write that could reach
       SFPCONFIG destination 15 (a typed dest-15 write, a call, an
       unknown asm word or Tensix instruction) disables the parity rule;
       otherwise the reset state, which the LLK initialization keeps, is
       assumed.  Mod0 10 (INT32_ALL) refuses: it couples the address to
       the Sp counter and changes it (SFPLOAD.md).
     - The Dst distance between rows is the schedule's absorbed stride.
       The advancing address mode is on the row's last issued word, every
       other access uses the no-increment mode, and SFPLOAD/SFPSTORE apply
       the address modifier after resolving their own address, so every
       access of row r resolves at the same counter value and row r+j is
       exactly j strides away.  Pending stores latch their Dst row at
       launch (L1), so later counter advances do not move them.

   The result is the smallest inter-row NOP count n (0 <= n < drain_slots)
   for which the follower rows at spacing n pass every rule, or
   drain_slots with the blocking reason reported.  Distances are stream
   slots, a lower bound on cycles (H2).  */

namespace {

/* One staged (launched) event with its realized footprint.  */
struct wp_event
{
  int exec;			/* in-row execution time: carrier word
				   position + 1 + decoded delay */
  xtt_subunit_t subunit;	/* realized sub-unit (byte position)   */
  unsigned sched_ix;		/* schedule/row-insn index of origin   */
  unsigned macro_index;
  unsigned kind;		/* SequenceBits case		       */
  bool vd16, route_vb;
  uint32_t lreg_read, lreg_write;
  bool cc_read, cc_write;
  bool cfg_write;
  bool dst_read, dst_write;
  bool dst_typed;		/* constant (addr, mod0) on record     */
  int dst_addr, dst_mod0;
  bool staging_copy;		/* the store-source staging copy; no
				   origin row instruction	       */
};

/* Dst physical-row model (see the comment above, and access_rows in
   gimple-rvtt-transp-involution.cc).  */

static unsigned
wp_access_rows (int addr, int mod0, unsigned rows[20])
{
  unsigned n = 0;
  unsigned r0 = ((unsigned) addr & 1023u) & ~3u;
  bool m32 = mod0 == 3 || mod0 == 4 || mod0 == 7 || mod0 == 9 || mod0 == 12
	     || mod0 == 10;
  bool m16 = !m32;		/* incl. mod0 0: config-resolved, union */
  if (mod0 == 0)
    m32 = true;
  for (unsigned r = r0; r != r0 + 4; ++r)
    {
      unsigned lane_row = r & 1023;
      unsigned adj = ((lane_row & 0x1F8) << 1) | (lane_row & 0x207);
      if (m32)
	{
	  rows[n++] = adj & 1023;
	  rows[n++] = (adj + 8) & 1023;
	}
      if (m16)
	{
	  rows[n++] = lane_row;		/* 16-bit layout */
	  rows[n++] = adj & 1023;	/* 32-bit layout via dst16->32 */
	  rows[n++] = (adj + 8) & 1023;
	}
    }
  return n;
}

/* Return true if the Dst accesses (ADDR_A, MOD0_A) and (ADDR_B, MOD0_B)
   touch disjoint physical row sets.  */

static bool
wp_rows_disjoint (int addr_a, int mod0_a, int addr_b, int mod0_b)
{
  unsigned ra[20], rb[20];
  unsigned na = wp_access_rows (addr_a, mod0_a, ra);
  unsigned nb = wp_access_rows (addr_b, mod0_b, rb);
  for (unsigned i = 0; i != na; ++i)
    for (unsigned j = 0; j != nb; ++j)
      if (ra[i] == rb[j])
	return false;
  return true;
}

/* Return true if the column-exchange LaneConfig bits stay at their
   default throughout FN: no possible writer of SFPCONFIG destination 15
   (typed dest-15 writes, calls, unknown asm words and Tensix instructions
   with unknown effects all count).  Scalar instructions cannot issue an
   SFPCONFIG.  */

static bool
wp_laneconfig_default_proved (function *fn)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  rtx pat = PATTERN (insn);
	  if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	    continue;
	  if (CALL_P (insn))
	    return false;
	  if (asm_noperands (pat) >= 0)
	    {
	      xtt_rwc_effect_t rwc;
	      if (!rvtt_raw_pure_dst_rwc (insn, &rwc))
		return false;
	      continue;
	    }
	  if (recog_memoized (insn) < 0)
	    return false;
	  if (get_attr_type (insn) != TYPE_TENSIX)
	    continue;
	  xtt_effect_set e = rvtt_insn_effects (insn);
	  if (e.opaque || (e.config_dests_written & (1u << 15)))
	    return false;
	}
    }
  return true;
}

/* Return true if two Dst accesses are disjoint.  LANECFG caches
   lanecfg_default_p (-1 unknown, 0 false, 1 true).  *WHY names the
   failing rule.  */

static bool
wp_dst_disjoint (const wp_event &a, const wp_event &b, function *fn,
		 int *lanecfg, const char **why)
{
  if (!a.dst_typed || !b.dst_typed)
    {
      *why = "window-pairing-dst-mode-unproven";
      return false;
    }
  if (wp_rows_disjoint (a.dst_addr, a.dst_mod0, b.dst_addr, b.dst_mod0))
    return true;
  /* Column-parity clause.  */
  if (((a.dst_addr >> 1) & 1) != ((b.dst_addr >> 1) & 1))
    {
      if (*lanecfg < 0)
	*lanecfg = wp_laneconfig_default_proved (fn) ? 1 : 0;
      if (*lanecfg == 1)
	return true;
      *why = "window-pairing-laneconfig-unproven";
      return false;
    }
  *why = "window-pairing-dst-alias";
  return false;
}

/* Return the blocker name for a data conflict between follower access F
   and pending event P whose order is not guaranteed, or null if they are
   independent.  */

static const char *
wp_conflict (const wp_event &f, const wp_event &p, function *fn, int *lanecfg)
{
  if ((f.lreg_write & (p.lreg_read | p.lreg_write))
      || (f.lreg_read & p.lreg_write))
    return "window-pairing-lreg-overlap";
  if ((f.cc_write && (p.cc_read || p.cc_write)) || (p.cc_write && f.cc_read))
    return "window-pairing-cc-live";
  if (f.cfg_write || p.cfg_write)
    return "window-pairing-config-overlap";
  if ((f.dst_write && (p.dst_read || p.dst_write))
      || (f.dst_read && p.dst_write))
    {
      const char *why = nullptr;
      if (!wp_dst_disjoint (f, p, fn, lanecfg, &why))
	return why;
    }
  return nullptr;
}

/* Set EV's constant Dst operands from ORIGIN, shifted by SHIFT strides.
   If they are unknown, EV is left untyped and any Dst pairing refuses.
   Mod0 10 (INT32_ALL) is never typed (see the comment above).  */

static void
wp_type_dst (rtx_insn *origin, const xtt_effect_set &e, int shift,
	     wp_event *ev)
{
  ev->dst_read = e.dst_mem_read;
  ev->dst_write = e.dst_mem_write;
  ev->dst_typed = false;
  if (!e.dst_mem_read && !e.dst_mem_write)
    return;
  rtx address, mode, addr_mode;
  if (!rvtt_dst_access_operands (origin, e, &address, &mode, &addr_mode)
      || !CONST_INT_P (address) || !CONST_INT_P (mode)
      || INTVAL (mode) == 10)
    return;
  ev->dst_typed = true;
  ev->dst_addr = (int) INTVAL (address) + shift;
  ev->dst_mod0 = (int) INTVAL (mode);
}

/* Complete EV's footprint from its origin instruction, its launch spec
   and its decoded SequenceBits fields, with its Dst address shifted by
   SHIFT strides.  Return false with *WHY set on failure.  */

static bool
wp_event_footprint (const macro_descriptor &desc, const rvtt_macro::caps *c,
		    rtx_insn *origin, int shift, wp_event *ev,
		    const char **why)
{
  xtt_effect_set e = rvtt_insn_effects (origin);
  if (e.opaque)
    {
      *why = "window-pairing-footprint-opaque";
      return false;
    }
  const macro_launch_spec *spec = nullptr;
  for (const macro_launch_spec &l : desc.launches)
    if (l.macro_index == ev->macro_index)
      {
	spec = &l;
	break;
      }
  if (!spec)
    {
      *why = "window-pairing-footprint-opaque";
      return false;
    }

  ev->lreg_read = e.lreg_read;
  ev->lreg_write = e.lreg_write;
  if (ev->kind != rvtt_macro::SEQ_CASE_STORE)
    {
      /* Simple/MAD/Round: VB or VC is overridden with the launch VD; the
	 result register is the launch VD or LReg[16].  */
      ev->lreg_read |= 1u << spec->vd;
      ev->lreg_write |= ev->vd16 ? (1u << 16) : (1u << spec->vd);
    }
  else
    {
      /* Store value register: LReg[16] (VD16), the template's own VD
	 (route bit; the origin already names it), or the launch VD.  */
      if (ev->vd16)
	ev->lreg_read |= 1u << 16;
      else if (!ev->route_vb)
	ev->lreg_read |= 1u << spec->vd;
    }
  if (ev->kind >= rvtt_macro::SEQ_CASE_TEMPLATE0
      && ev->kind - rvtt_macro::SEQ_CASE_TEMPLATE0 < desc.n_templates)
    ev->lreg_write |= rvtt_macro::template_hidden_lreg_writes
      (c, desc.templ[ev->kind - rvtt_macro::SEQ_CASE_TEMPLATE0]);

  ev->cc_read = e.cc_read;
  ev->cc_write = e.cc_write;
  ev->cfg_write = e.config_dests_written != 0 || e.addr_mod_slot_write;
  wp_type_dst (origin, e, shift, ev);
  return true;
}

/* Return true if the sequence slot of macro M at sub-unit position UNIT
   is the descriptor's store-source staging copy
   (staging_copy_realization: the launch VD copied into LReg16), which has
   no origin row instruction, and fill EV's footprint: it reads the launch
   VD (route_vb) and any template source field, and writes LReg16 plus the
   template's hidden writes.  */

static bool
wp_staging_copy_footprint (const macro_descriptor &desc,
			   const rvtt_macro::caps *c, unsigned m,
			   unsigned unit, wp_event *ev)
{
  rvtt_macro::staging_copy_facts copy;
  if (!rvtt_macro::staging_copy_realization (c, &copy)
      || unit != copy.seq_unit || !ev->vd16 || !ev->route_vb
      || ev->kind < rvtt_macro::SEQ_CASE_TEMPLATE0
      || ev->kind - rvtt_macro::SEQ_CASE_TEMPLATE0 >= desc.n_templates)
    return false;
  uint32_t word = desc.templ[ev->kind - rvtt_macro::SEQ_CASE_TEMPLATE0];
  rvtt_macro::template_spec tspec;
  if (!rvtt_macro::decode_template (word, &tspec)
      || tspec.opcode != copy.opcode || tspec.mod1 != copy.mod1
      || tspec.src_c >= 16)
    return false;
  const macro_launch_spec *spec = nullptr;
  for (const macro_launch_spec &l : desc.launches)
    if (l.macro_index == m)
      spec = &l;
  if (!spec)
    return false;
  ev->staging_copy = true;
  ev->lreg_read = (1u << spec->vd) | (1u << tspec.src_c);
  ev->lreg_write = (1u << 16)
    | rvtt_macro::template_hidden_lreg_writes (c, word);
  return true;
}

} /* anonymous namespace */

/* Return the minimal inter-row drain for REGION's uniform rows, in
   [0, desc.drain_slots]; desc.drain_slots (the full drain) on refusal,
   with the reason reported to DUMP.  When the result is positive but
   below the full drain, *BOUND_NAME names the conflict that prevents one
   NOP fewer.  */

int
rvtt_macro_interrow_drain_tuned (function *fn, const macro_region &region,
				 const macro_schedule &schedule,
				 const macro_descriptor &desc, FILE *dump,
				 const char **bound_name)
{
  *bound_name = nullptr;
  int full = desc.drain_slots;
  auto refuse = [&] (const char *name) -> int
    {
      rvtt_refuse_by_name (name, dump,
			   "Macro-planner window-pairing-refusal: %s\n",
			   name);
      return full;
    };

  if (full <= 0 || desc.cc.active || region.rows.length () < 2)
    return full;
  if (desc.keep_separator
      || (region.rows[0].separator && !schedule.absorbed_stride))
    return refuse ("window-pairing-separator-unproven");
  for (const macro_row &row : region.rows)
    if (row.insns.length () != schedule.events.length ())
      return refuse ("window-pairing-footprint-opaque");

  /* Rows may differ from row 0 in registers and operands, while the
     footprints below come from row 0.  Require every row's effect sets
     and Dst operands to be identical to row 0's (true for the
     fixed-VD calendars this targets); otherwise keep the full drain.  */
  for (unsigned r = 1; r < region.rows.length (); ++r)
    for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
      {
	rtx_insn *a = region.rows[0].insns[ix];
	rtx_insn *b = region.rows[r].insns[ix];
	xtt_effect_set ea = rvtt_insn_effects (a);
	xtt_effect_set eb = rvtt_insn_effects (b);
	if (ea.opaque || eb.opaque)
	  return refuse ("window-pairing-footprint-opaque");
	if (ea.lreg_read != eb.lreg_read || ea.lreg_write != eb.lreg_write
	    || ea.cc_read != eb.cc_read || ea.cc_write != eb.cc_write
	    || ea.config_dests_written != eb.config_dests_written
	    || ea.addr_mod_slot_write != eb.addr_mod_slot_write
	    || ea.dst_mem_read != eb.dst_mem_read
	    || ea.dst_mem_write != eb.dst_mem_write)
	  return refuse ("window-pairing-row-variance");
	if (ea.dst_mem_read || ea.dst_mem_write)
	  {
	    rtx addr_a, mode_a, am_a, addr_b, mode_b, am_b;
	    bool oa = rvtt_dst_access_operands (a, ea, &addr_a, &mode_a,
						&am_a);
	    bool ob = rvtt_dst_access_operands (b, eb, &addr_b, &mode_b,
						&am_b);
	    if (oa != ob)
	      return refuse ("window-pairing-row-variance");
	    if (oa
		&& (!rtx_equal_p (addr_a, addr_b)
		    || !rtx_equal_p (mode_a, mode_b)
		    || !rtx_equal_p (am_a, am_b)))
	      return refuse ("window-pairing-row-variance");
	  }
      }

  const rvtt_macro::caps *c = rvtt_macro_caps_for_cpu
    (TARGET_XTT_TENSIX_BH ? rvtt_macro::CPU_BH
     : TARGET_XTT_TENSIX_WH ? rvtt_macro::CPU_WH : rvtt_macro::CPU_QSR);
  if (!c)
    return refuse ("window-pairing-footprint-opaque");

  /* Issue-word positions in emission order (slots ascending), as in
     the boundary checks.  */
  auto_vec<int> word_pos;
  word_pos.safe_grow_cleared (schedule.events.length ());
  for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
    word_pos[ix] = -1;
  int carrier_pos[8];
  for (int m = 0; m != 8; ++m)
    carrier_pos[m] = -1;
  int words_per_row = 0;
  for (int slot = 0; slot != schedule.ii; ++slot)
    for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
      {
	const macro_event &ev = schedule.events[ix];
	if (!ev.issues_word || ev.slot != slot)
	  continue;
	word_pos[ix] = words_per_row;
	if (ev.is_carrier && ev.macro_index < 8)
	  carrier_pos[ev.macro_index] = words_per_row;
	++words_per_row;
      }
  if (words_per_row == 0)
    return refuse ("window-pairing-footprint-opaque");
  for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
    if (schedule.events[ix].issues_word && word_pos[ix] < 0)
      return refuse ("window-pairing-footprint-opaque");
  int last_pos = words_per_row - 1;

  /* Every Dst access of the row must resolve at one counter value,
     with the single advancing access on the row's last issued word, so
     consecutive rows are exactly one stride apart (SFPLOAD.md: the
     address resolves before the modifier applies).  The launches'
     emitted address modes are in the launch specs; explicit accesses
     use their own operands.  */
  int stride = schedule.absorbed_stride;
  /* Issue-word position of the single advancing access.  Normally it
     is the row's last issued word.  Under
     -mtt-tensix-optimize-window-pairing-stride it may be an earlier
     word, and every Dst footprint below is shifted by its word's stride
     phase (0 at or before the advancing word, 1 after it): that word's
     own access resolves before ApplyPartialAddrMod runs, and
     SFPLOADMACRO-hosted events latch their Dst row at the launch word
     (L1), so a word's position relative to the advancing word decides
     which counter value it sees (rvtt-cost.md, F5').  */
  int absorber_pos = last_pos;
  {
    int expected_advances = stride ? 1 : 0;
    int advances = 0;
    for (const macro_launch_spec &l : desc.launches)
      {
	if (l.addr_mode == c->no_increment_addr_mode)
	  continue;
	if (l.addr_mode != c->auto_increment_dst2_addr_mode
	    || l.macro_index >= 8
	    || carrier_pos[l.macro_index] < 0)
	  return refuse ("window-pairing-stride-unproven");
	if (carrier_pos[l.macro_index] != last_pos
	    && !riscv_tt_opt_window_pairing_stride)
	  return refuse ("window-pairing-stride-unproven");
	absorber_pos = carrier_pos[l.macro_index];
	++advances;
      }
    for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
      {
	const macro_event &ev = schedule.events[ix];
	if (ev.realization != macro_event::EXPLICIT_INSN || ev.is_carrier)
	  continue;
	rtx_insn *origin = region.rows[0].insns[ix];
	xtt_effect_set e = rvtt_insn_effects (origin);
	if (e.opaque)
	  return refuse ("window-pairing-footprint-opaque");
	if (!e.dst_mem_read && !e.dst_mem_write)
	  continue;
	rtx address, mode, addr_mode;
	if (!rvtt_dst_access_operands (origin, e, &address, &mode,
				       &addr_mode)
	    || !CONST_INT_P (addr_mode)
	    || INTVAL (addr_mode) != (int) c->no_increment_addr_mode)
	  return refuse ("window-pairing-stride-unproven");
      }
    if (advances != expected_advances)
      return refuse ("window-pairing-stride-unproven");
  }

  /* Stride phase of schedule event IX: 0 if its word is at or before
     the advancing word, 1 if after it, -1 if unknown.  The word is the
     event's own issued word, or its carrier's launch word for launched
     template slots (Dst row latched at launch, L1).  With the advance on
     the last word every phase is 0.  */
  auto stride_phase = [&] (unsigned ix) -> int
    {
      const macro_event &ev = schedule.events[ix];
      int carry = ev.issues_word ? word_pos[ix]
	: ev.macro_index < 8 ? carrier_pos[ev.macro_index] : -1;
      if (carry < 0)
	return -1;
      return carry > absorber_pos ? 1 : 0;
    };

  /* The staged events, decoded from the descriptor's SequenceBits and
     mapped back to their origin row instructions by the schedule's
     (macro, realized sub-unit) key.  Every launched schedule event must
     be accounted for exactly once, and the decoded horizon must equal
     the emitted drain.  */
  auto_vec<wp_event> staged;
  unsigned launched_total = 0;
  for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
    if (schedule.events[ix].realization
	== macro_event::LAUNCHED_TEMPLATE_SLOT)
      ++launched_total;
  int max_pend = 0;
  unsigned n_staging = 0;
  for (unsigned m = 0; m != desc.n_seq && m != 8; ++m)
    {
      uint8_t bytes[4];
      rvtt_macro::decompose_sequence_word (desc.seq[m], bytes);
      for (int i = 0; i != 4; ++i)
	{
	  unsigned kind, delay;
	  bool vd16, route_vb;
	  if (!rvtt_macro::decode_sequence_bits (bytes[i], &kind, &delay,
						 &vd16, &route_vb))
	    return refuse ("window-pairing-delay-unproven");
	  if (kind == rvtt_macro::SEQ_CASE_SKIP
	      || kind == rvtt_macro::SEQ_CASE_NOP)
	    continue;
	  if (carrier_pos[m] < 0)
	    return refuse ("window-pairing-footprint-opaque");
	  xtt_subunit_t su = i == 0 ? XTT_SU_SIMPLE
	    : i == 1 ? XTT_SU_MAD : i == 2 ? XTT_SU_ROUND : XTT_SU_STORE;
	  int found = -1;
	  for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
	    {
	      const macro_event &ev = schedule.events[ix];
	      if (ev.realization != macro_event::LAUNCHED_TEMPLATE_SLOT
		  || ev.macro_index != m)
		continue;
	      if ((xtt_subunit_t) rvtt_macro_hosted_subunit
		    (region.rows[0].insns[ix]) != su)
		continue;
	      if (found >= 0)
		return refuse ("window-pairing-footprint-opaque");
	      found = (int) ix;
	    }
	  wp_event ev;
	  memset (&ev, 0, sizeof (ev));
	  ev.macro_index = m;
	  ev.subunit = su;
	  ev.kind = kind;
	  ev.vd16 = vd16;
	  ev.route_vb = route_vb;
	  if (found < 0)
	    {
	      if (n_staging
		  || !wp_staging_copy_footprint (desc, c, m, i, &ev))
		return refuse ("window-pairing-footprint-opaque");
	      ++n_staging;
	    }
	  else
	    ev.sched_ix = (unsigned) found;
	  ev.exec = carrier_pos[m] + 1 + (int) delay;
	  if (ev.exec - last_pos > max_pend)
	    max_pend = ev.exec - last_pos;
	  staged.safe_push (ev);
	}
    }
  if (staged.length () != launched_total + n_staging)
    return refuse ("window-pairing-footprint-opaque");
  if (max_pend != full)
    return refuse ("window-pairing-delay-unproven");

  /* Pending horizon: the staged events still in flight after the
     row's last issued word, with their footprints at row r's own
     addresses.  */
  auto_vec<wp_event> pending;
  for (const wp_event &sv : staged)
    if (sv.exec > last_pos)
      {
	wp_event p = sv;
	p.exec = sv.exec - last_pos;	/* boundary-relative */
	if (sv.staging_copy)
	  {
	    pending.safe_push (p);
	    continue;
	  }
	int phase = stride_phase (sv.sched_ix);
	if (phase < 0)
	  return refuse ("window-pairing-stride-unproven");
	const char *why = nullptr;
	if (!wp_event_footprint (desc, c, region.rows[0].insns[p.sched_ix],
				 stride * phase, &p, &why))
	  return refuse (why);
	pending.safe_push (p);
      }

  /* Find the smallest spacing that works.  */
  int lanecfg = -1;
  const char *blocker_at[8] = {};
  int best = full;
  for (int n = 0; n < full; ++n)
    {
      const char *blocker = nullptr;
      for (unsigned j = 1; !blocker; ++j)
	{
	  /* Follower row j at spacing n: its first word issues at
	     boundary-relative slot base + 1.  Beyond the horizon every
	     access is late enough (H1, H2); run-tail rows are followed by
	     the full run-end drain.  */
	  int base = n + (int) (j - 1) * (words_per_row + n);
	  if (base + 1 > full)
	    break;
	  /* Front-end issued words are ordered after every pending
	     writeback at or before their issue slot (equality allowed by
	     retire-before-issue).  */
	  for (unsigned ix = 0; ix != schedule.events.length () && !blocker;
	       ++ix)
	    {
	      const macro_event &ev = schedule.events[ix];
	      if (!ev.issues_word)
		continue;
	      int t = base + 1 + word_pos[ix];
	      if (t >= full)
		continue;
	      wp_event f;
	      memset (&f, 0, sizeof (f));
	      f.exec = t;
	      xtt_effect_set e = rvtt_insn_effects (region.rows[0].insns[ix]);
	      if (e.opaque)
		{
		  blocker = "window-pairing-footprint-opaque";
		  break;
		}
	      int fphase = stride_phase (ix);
	      if (fphase < 0)
		{
		  blocker = "window-pairing-stride-unproven";
		  break;
		}
	      if (ev.is_carrier)
		{
		  /* The launch's own front-end SFPLOAD writes the launch VD
		     and reads its Dst address (including the store-only
		     carrier's sacrificial load).  */
		  const macro_launch_spec *spec = nullptr;
		  for (const macro_launch_spec &l : desc.launches)
		    if (l.macro_index == ev.macro_index)
		      spec = &l;
		  if (!spec)
		    {
		      blocker = "window-pairing-footprint-opaque";
		      break;
		    }
		  f.lreg_write = 1u << spec->vd;
		  xtt_effect_set fe = e;
		  fe.dst_mem_read = true;
		  fe.dst_mem_write = false;
		  wp_type_dst (region.rows[0].insns[ix], fe,
			       stride * ((int) j + fphase), &f);
		}
	      else
		{
		  f.lreg_read = e.lreg_read;
		  f.lreg_write = e.lreg_write;
		  /* The emission may retarget an explicit reload to its
		     planned register (the template source field or the
		     coalesced launch VD); use the union.  */
		  if (ev.realization == macro_event::EXPLICIT_INSN
		      && e.dst_mem_read)
		    for (unsigned jx = 0; jx != schedule.events.length ();
			 ++jx)
		      {
			const macro_event &cons = schedule.events[jx];
			xtt_effect_set ce
			  = rvtt_insn_effects (region.rows[0].insns[jx]);
			if (ce.opaque || !(ce.lreg_read & e.lreg_write))
			  continue;
			if (cons.realization == macro_event::CC_COALESCED
			    && !desc.launches.is_empty ())
			  f.lreg_write |= 1u << desc.launches[0].vd;
			else if (cons.realization
				   == macro_event::LAUNCHED_TEMPLATE_SLOT
				 && !cons.is_store
				 && cons.template_id < desc.n_templates)
			  {
			    rvtt_macro::template_spec tspec;
			    if (rvtt_macro::decode_template
				  (desc.templ[cons.template_id], &tspec)
				&& tspec.src_c)
			      f.lreg_write |= 1u << tspec.src_c;
			  }
		      }
		  f.cc_read = e.cc_read;
		  f.cc_write = e.cc_write;
		  f.cfg_write = e.config_dests_written != 0
		    || e.addr_mod_slot_write;
		  wp_type_dst (region.rows[0].insns[ix], e,
			       stride * ((int) j + fphase), &f);
		}
	      for (const wp_event &p : pending)
		{
		  if (p.exec <= t)
		    continue;		/* retired before this issue */
		  blocker = wp_conflict (f, p, fn, &lanecfg);
		  if (blocker)
		    break;
		}
	    }
	  /* Launched events of the follower row must be strictly after
	     every pending event; in the same cycle only distinct
	     sub-units with disjoint data are allowed.  */
	  for (unsigned sx = 0; sx != staged.length () && !blocker; ++sx)
	    {
	      int exec_f = base + 1 + staged[sx].exec;
	      if (exec_f > full)
		continue;	/* after every pending writeback */
	      wp_event f = staged[sx];
	      f.exec = exec_f;
	      if (!f.staging_copy)
		{
		  int fphase = stride_phase (f.sched_ix);
		  if (fphase < 0)
		    {
		      blocker = "window-pairing-stride-unproven";
		      break;
		    }
		  const char *why = nullptr;
		  if (!wp_event_footprint (desc, c,
					   region.rows[0].insns[f.sched_ix],
					   stride * ((int) j + fphase), &f,
					   &why))
		    {
		      blocker = why;
		      break;
		    }
		}
	      for (const wp_event &p : pending)
		{
		  if (exec_f > p.exec)
		    continue;		/* order preserved */
		  if (exec_f == p.exec && f.subunit == p.subunit)
		    {
		      blocker = "window-pairing-subunit-collision";
		      break;
		    }
		  blocker = wp_conflict (f, p, fn, &lanecfg);
		  if (blocker)
		    break;
		}
	    }
	}
      if (n < 8)
	blocker_at[n] = blocker;
      if (!blocker)
	{
	  best = n;
	  break;
	}
    }

  if (best == full)
    return refuse (blocker_at[0] ? blocker_at[0]
		   : "window-pairing-horizon-spill");
  if (best > 0)
    *bound_name = blocker_at[best - 1];
  return best;
}
