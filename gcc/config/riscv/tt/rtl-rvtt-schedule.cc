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

static bool
intersect_p (const HARD_REG_SET &a, const HARD_REG_SET &b)
{
  return hard_reg_set_intersect_p (a, b);
}

/* Move one independent ready instruction into a single exposed result-latency
   slot.  The existing delay pass runs afterward and remains the authority for
   target scoreboarding and WH/BH/QSR errata.  */
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

} // anonymous namespace

/* The generated target cost hook deliberately returns one for the existing
   STATIC/DYNAMIC contracts.  Do not generalize this to instruction distance:
   that needs a separate walk over emitted Tensix insns.  */
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
		rtx_insn *probe_insn, bool check_probe = false)
{
  if (bb->flags & BB_VISITED)
    return false;

  if (check_probe)
    {
      // Each block, other than the starting block, should only be
      // walked once -- don't get trapped in a loop of non-TENSIX
      // insns. The starting block should be walked exactly twice, if
      // reachable from itself.
      bb->flags |= BB_VISITED;
      visited.push_back (bb);
    }

  if (probe_insn)
    for (; probe_insn != NEXT_INSN (BB_END (bb));
	 check_probe = true, probe_insn = NEXT_INSN (probe_insn))
      {
	if (!check_probe)
	  continue;

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

  // Walk all the successors
  edge_iterator ei;
  edge e;
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (find_next_insn (visited, e->dest, regno, BB_HEAD (e->dest), true))
      return true;

  return false;
}

// Decide whether the nop inserter below would pad INSN's delay: the exact
// probe transform uses, factored out so the shadow-filling phase can target
// (and re-verify) precisely the bubbles that would otherwise become SFPNOPs.
// DELAY must be INSN's non-NONE delay contract.

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
      auto find_next = [] (auto self, std::vector<basic_block> &visited, basic_block bb,
			   rtx_insn *insn, rtx rtl) -> bool
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
		    // Writing to a constant reg falls on the floor
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

/* ---- Generalized latency-shadow filling ----

   fill_latency_bubbles above moves only the one instruction immediately
   behind an exposed result-latency slot.  That misses payloads whose only
   independent ready instruction sits deeper in the block: the nop inserter
   below then pads the bubble with an SFPNOP -- reissued on every playback
   when the padding lands inside a later replay capture.  This phase targets
   exactly the bubbles the inserter would pad (delay_nop_needed_p, the same
   probe, decides both before and after), and fills each with the first
   provably independent instruction found further down the block.  No new
   instruction is created; the move only reorders proven-independent
   operations.

   Safety vocabulary, refusing by default:
   - the filler must be a pure-LREG operation: every register reference an
     SFPU register, no memory, and one of (a) the unpredicated bare
     LREG-to-LREG copy (below), (b) audited XTT_LATENCY_REORDER_SAFE, or
     (c) on record in the typed effect table (rvtt_insn_effects) with no CC
     write, no configuration access, no RWC step, and no Dst traffic;
   - what a crossed instruction must prove depends on what the filler
     touches.  A hidden-state-free filler (the bare copy) is invariant to
     every piece of hidden state -- CC, Dst, RWC, configuration -- so a
     crossed instruction only has to be a recognized non-replay-owner
     Tensix instruction with DF-disjoint register sets: after register
     allocation every effect on an allocatable LREG must be visible in the
     pattern (a SET or CLOBBER), or allocation itself would be unsound, so
     DF reference sets are complete for allocatable registers; hidden
     effects can only target state this filler neither reads nor writes.
     A CC-reading (lane-predicated) filler additionally requires every
     crossed instruction to be provably non-CC-writing: one of the audited
     classes or a bare copy;
   - register independence is proved on DF hard-register references, with a
     predicated filler's writes also treated as reads (CC-disabled lanes
     preserve prior destination contents: read-modify-write);
   - a block containing an explicit replay-buffer owner refuses entirely:
     a fixed capture records the following delivered words by POSITION, so
     any reorder that straddles its extent would change the recording;
   - unrecognized, opaque, zero-length, or non-Tensix instructions end the
     search (the established barrier discipline);
   - the move commits only if the probe confirms the producer's bubble is
     closed, the filler opens none of its own, and no new bubble appears at
     the vacated position; otherwise it is undone, leaving the block
     byte-identical.

   Static delays are out of scope: they pad before any non-nop instruction,
   so no filler can close them.  Purely structural: no operation identity,
   opcode calendar, coefficient value, or instruction-word fingerprint
   participates.  */

/* The unpredicated LREG-to-LREG copy: the target's register-move pattern,
   emitted as the all-lanes SFPMOV variant.  It writes every lane of its
   destination and reads nothing but its source register -- no CC read or
   write, no Dst, RWC, or configuration access.  (Register allocation
   itself depends on exactly this full-copy semantics for spill copies
   inside predicated regions.)  */

static bool
bare_lreg_copy_p (rtx_insn *insn)
{
  rtx set = single_set (insn);
  return set && REG_P (SET_DEST (set)) && SFPU_REG_P (REGNO (SET_DEST (set)))
    && REG_P (SET_SRC (set)) && SFPU_REG_P (REGNO (SET_SRC (set)));
}

static bool
issued_tensix_p (rtx_insn *insn)
{
  return GET_CODE (insn) == INSN
    && GET_CODE (PATTERN (insn)) != USE
    && GET_CODE (PATTERN (insn)) != CLOBBER
    && recog_memoized (insn) >= 0
    && get_attr_type (insn) == TYPE_TENSIX
    && get_attr_length (insn) > 0;
}

/* SFPU-register references of INSN from DF; other references (scalar
   registers, memory) cannot conflict with a pure-LREG filler.  */

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

static bool
shadow_crossing_safe_p (rtx_insn *insn, bool hidden_free_filler)
{
  if (get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
    return false;
  if (hidden_free_filler)
    /* issued_tensix_p held at the caller: a recognized Tensix pattern's
       allocatable-register effects are DF-complete, and its hidden
       effects touch only state this filler is invariant to.  */
    return true;
  if (get_attr_xtt_latency_reorder (insn) == XTT_LATENCY_REORDER_SAFE)
    return true;
  if (bare_lreg_copy_p (insn))
    return true;
  xtt_effect_set e = rvtt_insn_effects (insn);
  return !e.opaque && !e.cc_write;
}

/* Returns nonzero for an admissible filler; *HIDDEN_FREE reports the bare
   unpredicated copy, whose crossing obligations are weaker.  */

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
  /* Read-modify-write conservatism for CC-predicated lane writes.  */
  regs->uses |= regs->defs;
  if (get_attr_xtt_latency_reorder (insn) == XTT_LATENCY_REORDER_SAFE)
    return true;
  xtt_effect_set e = rvtt_insn_effects (insn);
  return !e.opaque && !e.cc_write
    && !e.config_dests_written && !e.config_dests_read
    && e.rwc.kind == xtt_rwc_effect_t::NONE
    && !e.dst_mem_read && !e.dst_mem_write;
}

/* Filler-search window shared by the two fill phases below: an
   enumeration budget, NOT a cost-model constant (candidates beyond it
   are simply not considered -- refusal-direction only).  One definition
   so the two phases cannot drift (FH audit FHS-3).  */
constexpr unsigned SEARCH_WINDOW = 24;

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
	  /* An explicit replay owner ends the block's eligible region: a
	     fixed capture records the following delivered words by
	     position, so no later window may be reordered.  */
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
	      /* Bookkeeping-only insns emit no instruction word: USE/CLOBBER
		 markers and recognized zero-length ghosts.  They cannot fill
		 or separate anything, but their register references join the
		 crossed sets conservatively.  */
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
		  /* Crossing obligations depend on the filler's class, so
		     they are verified per candidate over the whole crossed
		     range.  */
		  bool crossable = true;
		  for (rtx_insn *x : crossed_insns)
		    if (!shadow_crossing_safe_p (x, hidden_free))
		      {
			crossable = false;
			break;
		      }

		  if (crossable)
		    {
		      /* Vacated-position guard data, gathered before
			 moving.  */
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

/* ---- Interlock-stall shadow fill (D3) ----

   fill_nop_shadows above targets bubbles the nop inserter would pad:
   the DYNAMIC delay contract, i.e. places where a required SFPNOP word
   would otherwise be emitted.  On targets with result scoreboarding
   (BH), most dependent back-to-back pairs need no NOP -- the hardware
   stalls transparently instead -- so those cycles never appear in the
   instruction stream, yet cost exactly one issue slot per occurrence
   inside a replayed capture.  This phase fills precisely those modeled
   stalls, using the audited `xtt_result_latency' fact family:

   - a stall is modeled ONLY between an issued producer with an AUDITED
     result latency of one and an immediately following issued consumer
     that references one of the producer's SFPU destinations (reads, or
     lane-predicated writes: disabled lanes preserve prior contents);
   - a producer with a dependent successor but an unaudited latency
     refuses byte-identically (dumped by name): with only the mad
     family audited the pass fires on nothing, which is the correct
     starting state -- value arrives class-by-class as audits land;
   - audited latencies above one have no entries today and refuse, so
     the two-adjacency stall accounting below is exact (NB the rvtt.md
     `xtt_result_latency' ATTRIBUTE is encoded latency+1 -- attr "2" IS
     latency one, not a latency-2 entry; two prior audits misread this,
     FH audit FHS-5);
   - required-nop bubbles (the DYNAMIC probe fires) stay owned by the
     nop inserter and fill_nop_shadows: this phase skips them;
   - the filler and every insn whose adjacency changes must themselves
     carry audited latencies, so the modeled stall count of the block
     provably decreases; otherwise the move is undone byte-identically;
   - filler admission and crossing obligations are exactly
     fill_nop_shadows' (shadow_filler_p / shadow_crossing_safe_p);
     an explicit replay owner ends the eligible region as before;
   - SFPSWAP's structural next-slot hazard (only SFPNOP is accepted in
     the following cycle) is NOT a consumable result latency: SFPSWAP
     carries no latency entry and therefore never becomes a fill
     target, and since every non-NOP successor of a swap costs the same
     one cycle, reordering around a swap leaves that cost invariant --
     it needs no term in the accounting;
   - QSR has no audited latency facts (the simulator refuses these
     opcode semantics): the pass refuses the whole target by name.

   Purely structural: no operation identity, opcode calendar,
   coefficient value, or instruction-word fingerprint participates.  */

/* Audited result latency of INSN in issue slots; -1 refuses (opaque
   effects or no audited `xtt_result_latency' entry).  */

static int
audited_latency (rtx_insn *insn)
{
  if (!issued_tensix_p (insn))
    return -1;
  xtt_effect_set e = rvtt_insn_effects (insn);
  // Lane BM (minimal, coordinated with the drain-model work): an
  // instruction with the architectural next-slot ACCEPTANCE stall
  // (xtt_next_slot_stall; SFPSWAP.md) keeps refusing here even once it
  // carries an audited result latency for the reissue-pricing model --
  // this preserves the pass's documented pre-audit behavior exactly
  // ("SFPSWAP ... never becomes a fill target").  That discipline,
  // like every timing rule, has ONE spelling: the item-#11 engine's
  // (verdict identity proven by the stage-A shadow over a full corpus
  // -fchecking leg, zero disagreements).
  return rvtt_timing::audited_latency (e.opaque, e.next_slot_stall,
				       e.result_latency);
}

/* Modeled interlock stall cycles between issued P and an immediately
   following issued C.  0 when independent (or either is missing); the
   audited latency when dependent; -1 (refuse) when dependent on an
   unaudited producer.  */

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
	  /* An explicit replay owner ends the block's eligible region.  */
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

	  /* Required-nop bubbles belong to the nop inserter and to
	     fill_nop_shadows; only transparent hardware stalls are
	     this phase's territory.  */
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
		  /* The filler occupies the stall slot: its own audited
		     latency must be zero so no new stall opens against
		     the consumer now one slot further away.  */
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
		      /* Modeled stall accounting over the adjacencies the
			 move changes.  The producer/cand and cand/consumer
			 pairs are proven independent above (crossed-set
			 discipline), so after the move only the vacated
			 seam can stall.  Any dependent-on-unaudited term
			 refuses.  */
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
			  /* Required-nop guards, exactly fill_nop_shadows'
			     discipline: the move must not manufacture a
			     new DYNAMIC-delay pad site.  */
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

/* ---- DAG list scheduling over audited straight-line SFPU regions ----

   The three phases above are single-move bubble fillers: each closes one
   exposed slot with one independent instruction.  They cannot express the
   general latency objective -- interleaving two independent dependence
   chains so every audited result-latency shadow is filled (the documented
   dual-Horner P/Q case: a serial P-chain-then-Q-chain stream carries one
   modeled stall per dependent adjacency; the interleaved stream carries
   none).  This phase is the real scheduler: it builds the dependence DAG
   of a maximal audited straight-line region and list-schedules it against
   the modeled issue timeline, committing the new order only on a strict
   modeled-makespan decrease.

   Region admission, refusing by name (fail-closed; every barrier bounds
   the region and keeps its own position, and nothing crosses it):
   - a node is an issued Tensix instruction that is pure-LREG (every
     register reference an allocatable SFPU register, no memory), either
     the bare unpredicated copy or on record in the typed effect table
     with no CC write, no configuration access, no RWC step, and no Dst
     traffic.  This is shadow_filler_p's effect vocabulary made STRICTER:
     no XTT_LATENCY_REORDER_SAFE acceptance (an audited-attribute class
     without a typed effect record never schedules here);
   - a node's result latency must be AUDITED (audited_latency >= 0, which
     also refuses the architectural next-slot acceptance stall) AND
     within the proven adjacency window (latency <= 1): the distance
     model below is exact only for the audited zero/one-slot facts, so a
     wider audit landing in the table refuses by name here
     ("latency-beyond-audited-window") until distance semantics are
     audited as their own fact family -- the same discipline as
     fill_interlock_shadows' by-name latency>1 refusal;
   - CC-writing instructions (setcc/encc and every other lane-state
     mutator) are named barriers: a region therefore executes under ONE
     CC state, so lane-predicated members read the same lane enables at
     any position inside it.  A lane-predicated write is a
     read-modify-write, but no explicit defs-join-uses conservatism is
     needed for the EDGE SET: the implicit read targets only the node's
     own destinations, and every ordering that read could demand is
     already carried latency-weighted by the WAW edge on the same
     register (earlier writer) or the WAR edge from the earlier reader's
     use set (later writer) -- the WAW/WAR edges subsume the RMW read;
   - a STATIC-delay contract is a named barrier (its pad precedes any
     non-nop instruction: reordering cannot close it);
   - an explicit replay-buffer owner ends eligibility for the REST of
     the block (a fixed capture records the following delivered words by
     position), matching the established phase discipline.  Formed
     macro-planner emissions are effect-opaque and therefore barriers.

   FORMATION INTERACTION (this pass runs BEFORE replay formation,
   dst-autoincr, and MOP formation, which consume the scheduled stream):
   - a SELF-LOOP block defers entirely, by name: a counted row executes
     back-to-back across the backedge (and, captured, across every
     playback), so the row is a CYCLE -- this scheduler's linear
     boundary model mispredicts the seam adjacency, which is capture
     rotation's audited territory;
   - REPEATED region shapes inside one block defer, by name: unrolled
     row copies must remain textually isomorphic for the replay
     former's re-roll and the MOP re-roll to recognize them, and
     boundary-context differences (a first copy's entry producer, a
     last copy's exit consumer) would otherwise schedule sibling copies
     differently.  Two regions with the same insn-code signature in one
     block therefore both refuse ("repeated-row shape deferred to
     replay capture formation").  Named residual: RUNTIME-unrolled
     copies living in separate blocks (branches between copies) evade
     both deferrals -- exact/counted unrolls land in one block and are
     covered; the corpus formation gate owns the residual.
   - Both deferrals are LIFTED, for exactly the shapes whose proofs
     hold, by -mtt-tensix-optimize-round-interleave (default off): a
     one-region self-loop row schedules under the wrapped steady-state
     II model, and an exactly-two isomorphic-copy family schedules as a
     pair under one shared permutation; every other shape keeps its
     deferral by name.  See the round-chain interleave section below.

   Dependence DAG, over DF hard-register references (complete for
   allocatable registers after allocation, as established above):
   - RAW and WAW edges require issue distance >= words(producer) +
     audited latency(producer); WAR edges require >= words(producer).

   Objective: modeled makespan of the region -- issue slots (multi-word
   instructions occupy their word count) plus modeled interlock stalls
   (the audited xtt_result_latency facts; on WH the same count appears
   as required SFPNOP words, on BH as transparent scoreboard stalls),
   plus the boundary terms:
   - the immediately preceding issued Tensix instruction (the entry
     producer) contributes its audited latency to nodes that touch its
     destinations; with every admitted latency <= 1, one issued
     instruction is the complete audited entry horizon (a producer two
     issue slots back has an expired shadow), and an entry producer
     carrying a latency above the window refuses into the pin
     discipline below;
   - an entry producer whose latency is UNAUDITED (or beyond the
     window) pins every region node that touches its destinations to
     its baseline issue slot or later ("entry-pinned"): the node's
     distance to the unknown-latency producer never decreases, so the
     unmodeled stall can only shrink.  Unknown-latency producers deeper
     than the entry adjacency are unmodeled in baseline and candidate
     alike -- the exposure class the fill phases already carry when a
     filler moves toward them.  Since the baseline-first node is always
     pinned to slot zero, a pinned node can occupy the region's first
     stream position only if it already held it, which is also what
     keeps the entry producer's DYNAMIC pad state from flipping (the
     commit guard below re-verifies it anyway);
   - the nearest following issued Tensix instruction (the exit consumer)
     adds the trailing shadow of the nodes that feed it; a region ending
     the block drains every node's shadow (conservative, applied to
     baseline and candidate identically).

   List scheduling itself is deterministic: ready nodes issue by
   greatest critical-path height (edge weights = the issue-distance
   requirements above), ties broken by original order.

   REGISTER PRESSURE: this pass runs post-allocation, where the eight
   allocatable hard LREGs themselves are the pressure bound -- register
   reuse appears as WAR/WAW edges that serialize the schedule, so no
   order this scheduler can emit needs a ninth name.  A pressure-aware
   dispatch over virtual registers is the pre-allocation scheduler's
   contract (the allocator lane), not this pass's; claiming one here
   would be a gate that cannot fire.

   The commit is transactional: the candidate order is adopted only on a
   strict modeled-makespan decrease, then re-verified against the nop
   inserter's own probe -- the count of DYNAMIC-delay pad sites over
   the region members must not grow, and the ENTRY producer's pad state
   must not flip on (the vacated-seam discipline of the fill phases);
   any failure restores the original chain exactly, debug insns
   included.  On a committed reorder debug insns keep their original
   chain links (codegen-identical; var-location bindings may drift, as
   under any scheduler).  Purely structural: no operation identity,
   opcode calendar, coefficient value, or instruction-word fingerprint
   participates.  */

namespace {

struct ls_node
{
  rtx_insn *insn;
  insn_regs regs;	 /* uses include defs for predicated writes  */
  HARD_REG_SET raw_defs; /* defs alone				      */
  int lat;		 /* audited result latency		      */
  int words;		 /* issue slots this instruction occupies    */
  int orig;		 /* original index within the region	      */
  long cp;		 /* critical-path height to region exit      */
  int ready;		 /* earliest issue slot (filled during sim)  */
  int entry_pin;	 /* issue slot floor from the entry boundary */
  bool pin_to_baseline;	 /* unaudited entry producer dependence      */
};

} // anonymous namespace

/* Node admission; returns false with *WHY naming the barrier class.  */

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
      /* No defs-join-uses conservatism for lane-predicated writes: the
	 RMW's implicit read targets only the node's own destinations,
	 and the latency-weighted WAW edge (against an earlier writer)
	 or the WAR edge from the earlier reader's use set (against a
	 later writer) already carries every ordering it could demand
	 (see the head comment).  */
    }
  node->lat = audited_latency (insn);
  if (node->lat < 0)
    {
      *why = "unaudited-latency";
      return false;
    }
  if (node->lat > 1)
    {
      /* The distance model is exact only for the audited zero/one-slot
	 adjacency facts (fill_interlock_shadows' discipline): a wider
	 audit refuses by name until distance semantics are their own
	 audited fact family.  */
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

/* Dependence test: does the earlier node P order the later node C?
   Kind: 0 none, 1 latency-weighted (RAW or WAW), 2 issue-order (WAR).  */

static int
ls_dependence (const ls_node &p, const ls_node &c)
{
  bool raw_or_waw = hard_reg_set_intersect_p (p.raw_defs, c.regs.uses)
		    || hard_reg_set_intersect_p (p.raw_defs, c.raw_defs);
  bool war = hard_reg_set_intersect_p (p.regs.uses, c.raw_defs);
  int kind = raw_or_waw ? 1 : war ? 2 : 0;
  /* Item-#11 verdict-identity shadow: one spelling of the RAW/WAW
     latency-weighted vs WAR issue-order classification.  */
  if (flag_checking)
    gcc_assert (kind
		== (int) rvtt_timing::classify_dependence (raw_or_waw, war));
  return kind;
}

/* Marshal the region NODES into the item-#11 engine's plain-data
   vocabulary: per-node {words, lat, entry_pin} plus the full
   dependence matrix (both directions; the diagonal carries the
   cross-copy self-dependence the cyclic model consumes).  */

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

/* Modeled issue timeline of NODES in the order given by ORDER (indices
   into NODES).  Fills issue slots into *ISSUE (indexed like NODES) and
   returns the modeled end: the last occupied slot boundary plus the
   trailing shadow of every node in EXIT_MASK (nodes feeding the exit
   consumer, or all nodes when the region ends the block).  Entry pins
   and the entry producer's latency floor are honored.  */

static int
ls_simulate (const std::vector<ls_node> &nodes,
	     const std::vector<int> &order, std::vector<int> *issue,
	     const std::vector<bool> &exit_shadow)
{
  int t = 0;
  for (unsigned k = 0; k != order.size (); ++k)
    {
      const ls_node &n = nodes[order[k]];
      int ready = n.entry_pin;
      for (unsigned j = 0; j != k; ++j)
	{
	  const ls_node &p = nodes[order[j]];
	  int kind = ls_dependence (p, n);
	  if (!kind)
	    continue;
	  int need = (*issue)[order[j]] + p.words + (kind == 1 ? p.lat : 0);
	  if (need > ready)
	    ready = need;
	}
      if (ready > t)
	t = ready;
      (*issue)[order[k]] = t;
      t += n.words;
    }
  int end = t;
  for (unsigned i = 0; i != nodes.size (); ++i)
    if (exit_shadow[i])
      {
	int drain = (*issue)[i] + nodes[i].words + nodes[i].lat;
	if (drain > end)
	  end = drain;
      }
  /* Item-#11 verdict-identity shadow: the unified engine's timeline
     must agree slot-for-slot before this simulator retires.  */
  if (flag_checking)
    {
      std::vector<int> chk_issue (nodes.size (), 0);
      int chk_end = rvtt_timing::simulate (ls_timing_seq (nodes), order,
					   &chk_issue, exit_shadow);
      gcc_assert (chk_end == end && chk_issue == *issue);
    }
  return end;
}

/* Count the DYNAMIC-delay pad sites over the region members (the nop
   inserter's own probe): the commit guard's before/after metric.  */

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

/* Deterministic list schedule of NODES honoring the DAG: critical-path
   priority, original order on ties.  Returns the chosen order.  */

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
	  /* Readiness: every earlier-original dependence already issued
	     and its distance satisfied.  */
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
	  /* Nothing ready this slot: advance to the earliest ready
	     time (a modeled stall).  */
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
   region.  UNAUDITED_DEFS is the entry-adjacent hazard: the entry
   producer's defs when its latency is outside the audited window
   (empty otherwise; deeper unknown-latency producers are unmodeled in
   both arms, see the head comment).  FORCED_ORDER, when given, replaces
   the list heuristic (the isomorphic-pair extension applies one
   region's chosen permutation to its sibling; legality is the caller's
   proven obligation via positional dependence-matrix equality);
   CHOSEN_ORDER, when given, receives the committed permutation.
   Returns true if the region was reordered (committed).  */

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

  /* Entry boundary: latency floor from the audited entry producer.
     An entry producer whose latency is unaudited or beyond the window
     contributes through UNAUDITED_DEFS (entry-adjacent, never a
     modeled floor).  */
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

  /* Exit boundary: nodes feeding the first following issued insn keep
     their trailing shadow in the makespan; a block-ending region drains
     everything.  */
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

  /* Baseline pins land AFTER the baseline itself is modeled.  */
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

  /* Commit guard data: the nop inserter's pad-site count over the
     region members, AND the ENTRY producer's pad state -- reordering
     changes which member is physically first, which can flip the pad
     need of the preceding dynamic-delay producer (the vacated-seam
     discipline of the fill phases, prev_needed_before).  */
  unsigned pads_before = ls_pad_sites (visited, bb, nodes);
  bool ep_dynamic
    = entry_producer
      && get_attr_xtt_delay (entry_producer) == XTT_DELAY_DYNAMIC;
  bool ep_needed_before
    = ep_dynamic
      && delay_nop_needed_p (visited, bb, entry_producer, XTT_DELAY_DYNAMIC);

  /* Exact-restore record: the chain from ANCHOR to the region's last
     member, debug insns included.  Notes are not recorded: a mid-block
     note can migrate relative to insns across a commit-then-restore
     (it emits no code; post-RA mid-block notes are rare).  */
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

/* ---- Round-chain interleave extensions (lane EI, default off) ----

   -mtt-tensix-optimize-round-interleave lifts the two formation
   deferrals above for exactly the shapes whose proofs hold, refusing
   the rest by name:

   (1) SELF-LOOP rows: the deferral exists because the linear boundary
       model mispredicts the backedge seam.  The cyclic extension
       replaces the boundary terms with the seam itself: acceptance is
       judged on the STEADY-STATE INITIATION INTERVAL of the wrapped
       dependence model (the body simulated as replicated back-to-back
       copies under the same issue-distance rules, converged when two
       successive iteration start distances agree -- the achieved-II of
       the makespan oracle's RecMII extension), committed only on a
       strict II decrease.  The reorder itself never crosses the
       backedge (per-iteration semantics are untouched), so
       bit-exactness holds exactly as in the straight-line case.
       Admission fails closed: the row must be ONE region with no other
       issued Tensix word (a seam barrier word breaks the modeled
       adjacency), no replay owner, and no call.  Cross-block producers
       into iteration one remain unmodeled in baseline and candidate
       alike -- the same exposure class the straight-line pass carries
       for block-head regions.  Before judging the interleave, a
       region-scoped storage-collision rename (ls_cyclic_rename_
       collisions, the lreg-rename pass's discipline) breaks the
       allocator-packed false WAW/WAR recurrences between the unrolled
       copies; a refusal restores the original registers exactly.

   (2) REPEATED (isomorphic) region pairs: the deferral exists because
       sibling copies scheduled differently stop being textually
       isomorphic for the replay/MOP re-roll.  For EXACTLY TWO regions
       sharing one insn-code signature whose positional dependence
       matrices are equal, ONE permutation (the first region's list
       order) is applied to both: the copies stay isomorphic by
       construction, each region is judged by its own boundary model,
       and the commit is transactional across the PAIR (a second-region
       refusal restores the first).  Three or more copies stay deferred
       to replay formation (its re-roll material), and unequal
       dependence matrices refuse by name
       ("copies-not-dataflow-isomorphic").  */

/* Queue every occurrence of hard reg OLDR inside *LOC as a
   validate_change to NEWR (the lreg-rename pass's replacement helper,
   restated here for the region-scoped rename below).  */

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

/* Storage-collision rename for the cyclic doubled row (round-interleave
   extension; the lreg-rename pass's discipline, region-scoped).  The
   allocator packs the two unrolled copies' short lifetimes into the
   same LREGs, manufacturing a false WAW/WAR recurrence that serializes
   the doubled row.  A colliding definition web -- node I defining reg
   R that an EARLIER region node also defines -- moves to a provably
   untouched LREG when:
   - R is not live INTO the block (excludes every loop-carried value:
     the self-loop's live-in is exactly the backedge-carried set);
   - the renamed value dies inside the region: a LATER region writer of
     R exists, or R is not live OUT of the block;
   - the defining write is not a read-modify-write of R (an implicit
     read of the colliding value never moves);
   - the target F is an allocatable LREG untouched by every region node
     (as currently composed), not live in or out of the block, and not
     fixed.
   The web = node I's definition plus every following reader of R, and
   THROUGH every read-modify-write redefinition of R (an in-place
   operation both consumes the renamed value and continues it --
   SFPMULI's destination IS its source -- so all its R occurrences
   move), ending exclusive before the next FRESH (non-reading) writer
   of R, which starts an unrelated value; reaching the region end
   instead needs R dead out of the block.  Every rename is recorded so
   a scheduling refusal restores the original registers EXACTLY (each
   web gets a fresh untouched F, so the inverse replacement F -> R over
   the region is unambiguous).  Value soundness under lane predication
   follows the region invariant the head comment establishes: a region
   executes under ONE CC state, every region write is masked by the
   same lane-enable set, and the region's outputs on disabled lanes
   come from pre-region register content, which the rename never
   touches (R keeps its pre-region content; F was dead).  */

struct ls_rename
{
  unsigned oldr, newr;
  std::vector<rtx_insn *> insns;	/* web members rewritten */
};

static void
ls_refresh_node_regs (std::vector<ls_node> &nodes)
{
  for (ls_node &nd : nodes)
    {
      collect_sfpu_regs (nd.insn, &nd.regs);
      nd.raw_defs = nd.regs.defs;
    }
}

static bool
ls_cyclic_rename_collisions (basic_block bb, std::vector<ls_node> &nodes,
			     std::vector<ls_rename> *record,
			     const std::vector<bool> *start_allowed = nullptr,
			     const std::vector<unsigned> *scan_order = nullptr)
{
  unsigned n = nodes.size ();
  bool any = false;
  /* SCAN_ORDER reorders only the ROOT search (which fresh definitions
     are offered the free registers first); web extents, collision
     detection and member rewrites stay in stream index order.  The
     cross-row pairing's stall-words extension passes the copy half
     first: the row-B webs are the ones whose serialization the pairing
     exists to break, so they must not be starved of free LREGs by an
     intra-row false-recurrence rename that buys far less.  */
  for (unsigned ii = 0; ii != n; ++ii)
    {
    unsigned i = scan_order ? (*scan_order)[ii] : ii;
    for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
      {
	if (!TEST_HARD_REG_BIT (nodes[i].raw_defs, r))
	  continue;
	if (start_allowed && !(*start_allowed)[i])
	  {
	    /* Caller-scoped lane-domain restriction (cross-row pairing):
	       a fresh definition inside a CC atom executes lane-predicated,
	       so renaming its web to a dead LREG would leave stale disabled-
	       lane bits in the new register where the original register
	       carried the pre-atom value -- a later all-lanes consumer or
	       store could expose them.  Only webs rooted in the proven
	       ambient all-lanes state may rename (they write every lane at
	       the root, so the fresh register never exposes dead bits).  */
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
	    /* An RMW definition reads the COLLIDING value: the chain
	       start must be a fresh value.  */
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
	/* Web extent: from the fresh definition at I, forward through
	   every reader of R and THROUGH every read-modify-write
	   redefinition of R (an in-place operation both consumes the
	   renamed value and continues it -- SFPMULI's dest IS its
	   source -- so its R occurrences all move), ending exclusive
	   before the next FRESH (non-reading) writer of R, which
	   starts an unrelated value.  Reaching the region end without
	   such a writer needs R dead out of the block.  */
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
	    rvtt_refuse (RVTT_REF_ROUND_INTERLEAVE_RENAME_NO_FREE_LREG, dump_file,
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

/* Undo every recorded rename exactly (each web's F was untouched
   before, so replacing F back with R over the web is unambiguous).  */

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

/* Steady-state initiation interval of NODES issued repeatedly in ORDER:
   the wrapped (cyclic) issue model of a self-loop row.  Entry pins do
   not apply (the seam is the model); dependences reach across copies
   through the same ls_dependence vocabulary.  */

static int
ls_cyclic_ii (const std::vector<ls_node> &nodes,
	      const std::vector<int> &order)
{
  unsigned n = nodes.size ();
  const unsigned COPIES = 6;
  std::vector<int> issue (n * COPIES, 0);
  std::vector<int> start (COPIES, 0);
  int t = 0;
  int last_d = 0;
  for (unsigned c = 0; c != COPIES; ++c)
    {
      for (unsigned k = 0; k != n; ++k)
	{
	  const ls_node &nd = nodes[order[k]];
	  int ready = t;
	  for (unsigned pc = 0; pc <= c; ++pc)
	    for (unsigned j = 0; j != (pc == c ? k : n); ++j)
	      {
		const ls_node &p = nodes[order[j]];
		int kind = ls_dependence (p, nd);
		if (!kind)
		  continue;
		int need = issue[pc * n + order[j]] + p.words
			   + (kind == 1 ? p.lat : 0);
		if (need > ready)
		  ready = need;
	      }
	  issue[c * n + order[k]] = ready;
	  t = ready + nd.words;
	  if (k == 0)
	    start[c] = ready;
	}
      if (c >= 2)
	{
	  int d1 = start[c] - start[c - 1];
	  int d2 = start[c - 1] - start[c - 2];
	  last_d = d1;
	  if (d1 == d2)
	    {
	      /* Item-#11 verdict-identity shadow.  */
	      if (flag_checking)
		gcc_assert (rvtt_timing::cyclic_ii (ls_timing_seq (nodes),
						    order) == d1);
	      return d1;
	    }
	}
      else if (c == 1)
	last_d = start[1] - start[0];
    }
  /* Item-#11 verdict-identity shadow.  */
  if (flag_checking)
    gcc_assert (rvtt_timing::cyclic_ii (ls_timing_seq (nodes), order)
		== last_d);
  return last_d;
}

/* Cyclic scheduling of the single region of a self-loop row.
   Transactional exactly like ls_schedule_region; acceptance = strict
   steady-state II decrease; the same pad-site commit guard applies
   (the nop inserter's probe is the WH correctness carrier and must not
   grow).  */

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

  /* Guard metric and baseline on the ORIGINAL code, before any
     rename.  */
  unsigned pads_before = ls_pad_sites (visited, bb, nodes);
  std::vector<int> base_order (n);
  for (unsigned i = 0; i != n; ++i)
    base_order[i] = i;
  int base_ii = ls_cyclic_ii (nodes, base_order);

  /* Break storage-induced false recurrences before judging the
     interleave; every rename is undone on refusal.  */
  std::vector<ls_rename> renames;
  ls_cyclic_rename_collisions (bb, nodes, &renames);

  std::vector<int> order = ls_list_order (nodes);
  int cand_ii = ls_cyclic_ii (nodes, order);

  if (cand_ii >= base_ii)
    {
      ls_undo_renames (renames);
      if (dump_file)
	fprintf (dump_file, "List-schedule refused: no modeled "
		 "steady-state II decrease in bb %d cyclic region at "
		 "uid=%d (%d -> %d)\n",
		 bb->index, INSN_UID (nodes[0].insn), base_ii, cand_ii);
      return false;
    }

  /* Exact-restore record, debug insns included.  */
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
      fprintf (dump_file, "List-schedule (round-interleave cyclic): "
	       "bb %d nodes=%u II %d -> %d renames=%zu target=%s\n",
	       bb->index, n, base_ii, cand_ii, renames.size (),
	       TARGET_XTT_TENSIX_WH ? "wh" : "bh");
      for (unsigned k = 0; k != n; ++k)
	fprintf (dump_file, "List-schedule slot-order=%u uid=%d\n",
		 k, INSN_UID (nodes[order[k]].insn));
    }
  return true;
}


/* The entry producer of a region starting at FIRST: the nearest
   preceding instruction the DYNAMIC pad probe (find_next_insn) would
   treat as word-adjacent to the region's first member.  The probe
   SKIPS non-Tensix insns, USE/CLOBBER markers, and non-dependent
   zero-length ghosts, so a scalar RISC insn in the gap (a loop
   counter, an address materialization) does NOT break the adjacency:
   the walk here skips exactly the probe's vocabulary, or the entry
   pin, latency floor, and pad-flip guard would all silently bypass
   across a one-scalar gap.  */

static rtx_insn *
ls_entry_producer (basic_block bb, rtx_insn *first)
{
  for (rtx_insn *w = PREV_INSN (first); w && w != PREV_INSN (BB_HEAD (bb));
       w = PREV_INSN (w))
    {
      if (!NONDEBUG_INSN_P (w))
	continue;
      if (GET_CODE (w) != INSN)
	/* Jump/call boundary.  KNOWN DIVERGENCE from the dynamic pad
	   probe (DU-S8(a), still open): find_next_insn walks THROUGH a
	   call while this walk stops, so a region entered right after a
	   call sees no entry producer and keeps the conservative
	   latency floor -- refusal-direction only (a fill opportunity
	   is missed, never a hazard admitted).  */
	return nullptr;
      rtx pat = PATTERN (w);
      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	continue;
      if (recog_memoized (w) < 0)
	return nullptr;
      if (get_attr_type (w) != TYPE_TENSIX)
	continue;		/* scalar: probe-transparent */
      if (!get_attr_length (w))
	continue;		/* zero-length marker: no word, no event */
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
				   latency is out of the audited window */
  std::vector<int> signature;	/* insn codes, for the repeat deferral */
};

/* Isomorphic-pair scheduling (round-interleave extension): apply R1's
   chosen permutation to R2, keeping the copies textually isomorphic
   for the replay/MOP re-roll.  Legality of the shared permutation
   rests on positional dependence-matrix equality, proven before any
   motion; each region is judged by its own boundary model; the commit
   is transactional across the PAIR (a second-region refusal restores
   the first exactly).  */

static void
ls_schedule_iso_pair (basic_block bb, ls_region &r1, ls_region &r2,
		      std::vector<basic_block> &visited)
{
  unsigned n = r1.nodes.size ();
  if (r2.nodes.size () != n)
    return;	/* signatures equal implies equal sizes; belt only.  */

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

  /* First region's exact-restore record, captured BEFORE its commit so
     a second-region refusal can undo the pair.  */
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
    return;	/* first copy refused; nothing moved.  */

  if (!ls_schedule_region (bb, r2.nodes, r2.anchor, r2.entry_producer,
			   next_issued_insn (bb, r2.nodes.back ().insn),
			   r2.unaudited_defs, visited, &order, nullptr))
    {
      /* Restore the first region exactly: pair-transactional.  */
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

/* ---- Cross-row pairing (-mtt-tensix-optimize-crossrow-pairing) ----

   The FI-3c mechanism: pair two consecutive iterations of a capturable
   single-row Dst loop into ONE doubled row whose iterations interleave,
   keeping the counted-loop replay capture shape so delivery stays
   record-plus-launch (halved launches) while the interleave fills the
   modeled dependency stalls the single row cannot (the roundingops
   mad->setcc distance-1 adjacency and the seam; capture rotation names
   and cannot fill them -- every single-row filler is CC-bearing).

   Admitted shape (everything else refuses by name, fail-closed, and the
   single row is kept byte-identically):

     row:   one constant-address no-increment Dst load FIRST, pure/CC
	    words, one matching constant-address no-increment Dst store
	    LAST, all words audited (typed effects, audited 0/1-slot
	    latency, one issue slot, SFPU-only DF references, fixed
	    replay encodings);
     CC:    flat atoms only -- each opens at a CC writer and closes at
	    the word-exact all-lanes SFPENCC restore (cc_write_all_lanes);
	    no Dst or RWC effect inside an atom; ambient state between
	    atoms is all-lanes, PROVEN at loop entry by a backward walk
	    (nearest reaching CC writer is the all-lanes restore, or the
	    walk reaches the function entry, whose all-lanes ambient is
	    the shipped structured-CC lowering contract: gimple-rvtt-cc.cc
	    removes the outermost PUSHC and closes every outermost region
	    with the exact all-lanes ENCC);
     step:  one trailing typed TTINCRWC (0, d, 0, 0) row separator, the
	    only RWC effect in the loop;
     ctrl:  the canonical scalar countdown (reg += -1; if (reg != 0)
	    backedge) whose register is referenced nowhere else, counting
	    down from a proven EVEN positive constant.

   Transform (one transaction; every later refusal restores exactly):

     1. row B = textual copy of row A, emitted after A;
     2. B's load/store Dst addresses rebase A -> A+d (the typed static
	offset the removed interior row step would have supplied), the
	shared trailing separator doubles d -> 2d, and the countdown
	halves -1 -> -2: the pair touches exactly the Dst rows and the
	RWC walk the two original iterations touched, in the same
	counter frame (disjointness: both footprints are constant-offset
	in one frame, A aligned 0 mod 4 and B at A+d with d = 2 address
	units, so the two rows' unit footprints cannot overlap);
     3. allocator-packed row-B webs rename to dead LREGs through the
	established transactional cyclic renamer, restricted to webs
	rooted in the ambient all-lanes state (rename-cc-domain: a
	fresh predicated definition renamed to a dead register would
	expose stale disabled-lane bits -- the adjudicated defect of the
	round-cc-modulo prototype, NO-GO 2026-08-25);
     4. pure spans of the two rows list-schedule together interval by
	interval; CC atoms stay indivisible, in original interior order,
	atom A before atom B (each atom computes its own lane state from
	its own row's data -- contiguity is the CC-state-equality
	placement proof); stores stay in architectural order;
     5. acceptance: strict modeled steady-state II decrease over the
	doubled baseline (the two logical iterations cost-compared in
	the same delivery mode) AND no pad-site increase (the nop
	inserter's probe) AND the doubled row still fits the replay
	buffer (2n <= XTT_DELIVERY_CAPTURE_SLOTS with the separator
	explicit), so the counted-loop capture downstream keeps firing
	and the transform never trades the replay delivery for issue
	slots (the adjudicated profitability defect of the prototype).

   Purely structural: no operation identity, opcode calendar,
   coefficient value, or instruction-word fingerprint participates.
   Blackhole only (the audited latency/adjacency model family).  */

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

/* Mirror of rtl-rvtt-replay.cc fixed_replay_rtx_p (the capture pass's
   own fixed-encoding admission): hard LREGs, constants and scratch are
   fixed; a GPR or MEM means the word cannot be recorded.  Kept in step
   so the capture-shape precondition proven here is the one the
   downstream counted-loop capture re-checks.  */

static bool
crp_fixed_word_p (const_rtx x)
{
  switch (GET_CODE (x))
    {
    case CONST_INT:
    case SCRATCH:
      return true;
    case REG:
      return SFPU_REG_P (REGNO (x));
    case SET:
      return crp_fixed_word_p (SET_DEST (x)) && crp_fixed_word_p (SET_SRC (x));
    case CLOBBER:
    case USE:
      return crp_fixed_word_p (XEXP (x, 0));
    case PARALLEL:
    case UNSPEC:
    case UNSPEC_VOLATILE:
      for (int ix = XVECLEN (x, 0); ix--;)
	if (!crp_fixed_word_p (XVECEXP (x, 0, ix)))
	  return false;
      return true;
    default:
      return false;
    }
}

/* Row-word admission into the pairing's scheduling vocabulary.  The
   node model is the list scheduler's own (audited 0/1-slot latencies,
   LREG dependence edges); CC words enter with their real LREG uses for
   RAW ordering -- their CC visibility is protected by the indivisible
   atom and its original interior order, and the architectural lag is
   exactly one (rvtt_macro::cc_visibility_lag).  */

static bool
crp_node (basic_block bb, rtx_insn *insn, ls_node *node)
{
  if (!issued_tensix_p (insn) || JUMP_P (insn)
      || get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
    return crp_refuse (bb, "crossrow-pairing-foreign-word", insn);
  if (get_attr_xtt_replay (insn) != XTT_REPLAY_SAFE
      || !crp_fixed_word_p (PATTERN (insn)))
    return crp_refuse (bb, "crossrow-pairing-noncapturable-word", insn);
  xtt_effect_set e = rvtt_insn_effects (insn);
  /* Next-slot acceptance-stall words (the SFPSWAP family) join the
     vocabulary under the sub-flag: the word is fully audited (its
     biased xtt_result_latency is on record; an unaudited one still
     refuses below) and its architectural stall is PRICED, not proven
     away -- two issue slots in the steady-state II model (the
     rvtt-cost.md consumer rule: one extra slot per occurrence),
     charged identically in the doubled sequential baseline and every
     candidate.  audited_latency () itself keeps returning -1 for
     these words (lane BM): the interlock scheduler and capture
     rotation never gain them as fill participants.  */
  bool stall_word = e.next_slot_stall
    && riscv_tt_opt_crossrow_pairing_stall_words > 0;
  if (e.opaque || e.config_dests_written || e.config_dests_read
      || e.addr_mod_slot_write || (e.next_slot_stall && !stall_word)
      || get_attr_xtt_delay (insn) == XTT_DELAY_STATIC)
    return crp_refuse (bb, "crossrow-pairing-effect-unproven", insn);
  /* Every DF reference must be an SFPU register: a scalar (GPR)
     dependence is outside this vocabulary and would be reordered
     untracked.  */
  for (df_ref ref = DF_INSN_USES (insn); ref; ref = DF_REF_NEXT_LOC (ref))
    if (DF_REF_REGNO (ref) >= FIRST_PSEUDO_REGISTER
	|| !SFPU_REG_P (DF_REF_REGNO (ref)))
      return crp_refuse (bb, "crossrow-pairing-scalar-dependence", insn);
  for (df_ref ref = DF_INSN_DEFS (insn); ref; ref = DF_REF_NEXT_LOC (ref))
    if (DF_REF_REGNO (ref) >= FIRST_PSEUDO_REGISTER
	|| !SFPU_REG_P (DF_REF_REGNO (ref)))
      return crp_refuse (bb, "crossrow-pairing-scalar-dependence", insn);
  /* A stall word bypasses audited_latency ()'s deliberate -1 (its
     refusal there is the fill-participation contract, not a missing
     audit): the biased attribute value is the audited result latency,
     and an UNAUDITED one (-1) still refuses through the range check
     below.  */
  node->lat = stall_word ? e.result_latency : audited_latency (insn);
  /* Pure flag writers have no LREG result whose latency enters the
     list model; their ordering is the atom's.  */
  if ((node->lat < 0 || node->lat > 1) && e.cc_write && !e.lreg_write
      && rvtt_macro::cc_visibility_lag () == 1)
    node->lat = 0;
  bool regs_ok = collect_sfpu_regs (insn, &node->regs);
  if (!regs_ok && (e.cc_write || e.dst_mem_write) && !e.lreg_write)
    {
      /* Defless CC/store words fail collect_sfpu_regs' ordinary
	 schedulable-node contract (no LREG destination); the explicit
	 SFPU-only DF proof above already excluded scalar forms, so
	 retain their real LREG uses for RAW ordering.  */
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
  /* The acceptance stall is an ISSUE fact, not a stream word: the
     node occupies two slots in the II/greedy time accounting while
     the capture-budget bound (a per-NODE count of recorded words)
     stays one.  */
  if (stall_word)
    node->words = 2;
  node->entry_pin = 0;
  node->pin_to_baseline = false;
  return true;
}

/* Prove the ambient lane state at the loop's entry is all-lanes: walk
   backward from the loop's dedicated preheader through single-
   predecessor blocks; the nearest reaching CC writer must be the
   word-exact all-lanes restore, or the walk reaches the function entry
   (the shipped structured-CC lowering contract's ambient).  Anything
   opaque to the CC vocabulary refuses.  */

static bool
crp_entry_all_lanes_p (basic_block bb, basic_block preheader)
{
  basic_block cur = preheader;
  while (true)
    {
      for (rtx_insn *insn = BB_END (cur); insn && insn != PREV_INSN (BB_HEAD (cur));
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
	    continue;	/* scalar work carries no lane state */
	  if (!get_attr_length (insn))
	    continue;	/* bookkeeping ghost */
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

/* Structural admission of the whole loop.  Silent (returns false with
   no dump line) only when BB is not a self-loop at all.  */

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
      /* Scalar RISC insn: admit exactly the canonical countdown, after
	 the separator.  */
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

  /* Dst disjointness of the paired footprints: both accesses at one
     constant address A in one counter frame, the copy at A+d with the
     separator's own per-iteration advance d (2 address units = one
     row); alignment and range keep the unit footprints disjoint and
     the rebased address encodable.  */
  xtt_effect_set le = rvtt_insn_effects (lp->nodes[lp->load].insn);
  xtt_effect_set se = rvtt_insn_effects (lp->nodes[lp->store].insn);
  rtx la, lm, lam, sa, sm, sam;
  HOST_WIDE_INT addr_limit = 8191;	/* BH imm10-class Dst address */
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

  /* Counter provenance: the loop's dedicated preheader must seed the
     countdown with an even positive constant (halving -1 -> -2 then
     preserves the NE-0 exit exactly), and the register is referenced
     nowhere else in the row (the SFPU-only proof above covers every
     row word; the separator is all-constant).  */
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

  /* Ambient lane state at loop entry.  */
  if (!crp_entry_all_lanes_p (bb, preheader))
    return false;

  return true;
}

/* Queue a typed Dst address replacement on a load/store copy.  Operand
   identity is used only after effect admission, mirroring
   rvtt_dst_access_operands' positional contract.  */

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

/* Candidate order construction: a dependence-legal global list
   schedule over ITEMS, where each CC atom is one indivisible super-item
   (its words emit contiguously in original interior order -- the
   CC-state-equality placement proof) and every other row word is its
   own item.  Item dependence uses the aggregated register sets through
   the established ls_dependence vocabulary with original (sequential
   two-iteration) order as the dependence direction, so an UNRENAMED
   shared web serializes exactly as the two original iterations would
   (the round-cc-modulo prototype's span construction ignored these
   edges and could order a copy's redefinition ahead of the first row's
   store -- the CRAQ-caught WAR defect this constructor removes by
   construction).  */

struct crp_item
{
  std::vector<unsigned> members;	/* indices into ALL, in order */
  HARD_REG_SET uses, raw_defs;
  int words;
  int lat;				/* conservative max member latency */
  unsigned orig;			/* first member's original index */
};

static bool
crp_item_dep (const crp_item &p, const crp_item &c)
{
  return hard_reg_set_intersect_p (p.raw_defs, c.uses)
    || hard_reg_set_intersect_p (p.raw_defs, c.raw_defs)
    || hard_reg_set_intersect_p (p.uses, c.raw_defs);
}

static std::vector<rtx_insn *>
crp_candidate_order (const std::vector<ls_node> &all,
		     const std::vector<int> &group)
{
  /* Build the item list in original (sequential two-iteration) order:
     GROUP assigns every node its atom instance (a maximal run of one
     non-negative id is one indivisible super-item; -1 nodes -- pure
     words and preservation seeds -- are their own items).  */
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

  /* Deterministic greedy list schedule over the items: modeled issue
     time from the item dependence edges (latency-weighted like
     ls_dependence kind 1 for RAW/WAW; issue-order for WAR is the same
     conservative bound here since items are multi-word), earliest
     ready first, critical original order on ties.  Dependence direction
     is original order, so the result is legal by construction.

     Under the stall-words extension the selection among READY items is
     critical-path first (ls_list_order's own priority rule, applied at
     the item granularity): the plain earliest-ready rule drains the
     row-A tail while row B's longer remaining chain is the critical
     path, leaving row B's SFPMUL->SFPSWAP delay shadow bare at the end
     of the body -- one literal SFPNOP, which both costs the modeled
     slot and can push the doubled record past the replay capture
     budget (the capture-overflow belt below).  A critical-path
     selection interleaves the two tails the way the hand kernels do.
     Ties stay on original order; legality is unchanged (the belt
     re-verifies every dependence direction).  */
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
		 readiness only wins when the earlier item's issue
		 cannot overlap the other's wait (both comparisons stay
		 deterministic: ties fall to original order).  */
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

/* Legality belt: every original-order dependence must keep its
   direction in the candidate.  Returns false on any violation (the
   caller refuses by name; with the constructor above this cannot
   fire, but the pairing never trusts its own scheduler).  */

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

/* ---- Shared-reload dedupe (-mtt-tensix-optimize-crossrow-shared-reload)

   The lane-IC residual: the doubled row carries the copy half's
   duplicated in-loop constant materializations (the tanh anatomy: two
   loadi def-groups per half into ONE reload register) because the
   position-blind hard-reg web vocabulary cannot express one half's
   consumer reading the OTHER half's earlier definition.  A naive
   dedupe -- delete the copy's definitions, keep its consumers -- is
   wrong code BEFORE any scheduling: in the sequential original order
   the surviving consumer's nearest preceding definition of the shared
   register is the first half's NEXT-epoch materialization (tanh: row
   B's C3-mad would read row A's C1 loadi), and ls_dependence derives
   value flow from position alone; no edge in the vocabulary can say
   "read the earlier definition".

   The sound form makes position value-correct again: split both
   halves at each definition group into epoch segments and RE-SEQUENCE
   the pairing's original order epoch by epoch (the first half's
   segment, then the copy's with its definitions deleted).  In the
   merged order every surviving consumer sits between its own epoch's
   definition group and the next one, so the established name-based
   vocabulary derives exactly the sharing constraints from position --
   RAW from the epoch definition into both halves' consumers, WAR from
   the copy's consumers into the next epoch's definition -- the greedy
   scheduler cannot commit a value-breaking order (crp_order_legal_p
   re-verifies every edge), and the value-oracle belt below re-walks
   the committed order against the epoch assignment independently.

   Value equivalence of the merged order to the sequential doubled
   baseline: (1) the copy half's definition groups are word-for-word
   identical to the first half's (the textual-copy fact, re-verified
   byte-for-byte after every rename -- a rename that touched either
   web refuses), so a deleted definition's value IS the surviving one;
   (2) the shared register is dead into and out of the loop and every
   consumer follows its group's last member, so each consumer reads
   exactly its epoch's completed 32-bit image; (3) the merge only
   moves copy-half words ahead of LATER-segment first-half words, and
   any such reordered pair must be free of register interaction beyond
   the shared register itself (the interference refusal: the halves'
   other webs are disjoint after renaming, or read-only) -- disjoint
   accesses commute, and the shared register's cross-half readings are
   exactly (1)+(2).  Across iterations the committed record is one
   linear word stream: the next iteration's first definition group
   follows this iteration's last consumer in stream order, the same
   single-register recycling the original row performed.  */

struct crp_shared_reload_info
{
  unsigned reg = ~0u;			/* shared reload register, or ~0u */
  unsigned removed = 0;			/* deleted copy-half def words */
  /* Epoch (1-based) per participating insn, for the value-oracle
     re-verification of the committed order.  */
  std::vector<std::pair<rtx_insn *, unsigned> > def_epoch;
  std::vector<std::pair<rtx_insn *, unsigned> > consumer_epoch;
};

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

/* Candidate order plus modeled steady-state II over ALL/GROUP (the
   pairing's own construction and model); INT_MAX on any construction
   failure.  */

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

/* Value-oracle re-verification of the committed candidate order: walk
   the final order and check that the definition groups appear whole,
   in epoch order, and that every surviving consumer's nearest
   preceding definition state is exactly its assigned, completed
   epoch.  Independent of the dependence engine (the pairing never
   trusts its own scheduler).  */

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
					   epoch's register image */
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

/* The dedupe proper.  Analyzes the doubled row (ALL/GROUP hold 2*N
   nodes: the first half's words then the copy's), and on full
   admission deletes the copy half's definition groups, re-sequences
   ALL/GROUP into the epoch-merged original order, and fills *OUT for
   the value-oracle belt.  Every unproven piece refuses by name and
   leaves the duplicated pairing untouched.  */

static void
crp_shared_reload (basic_block bb, const crp_loop &lp,
		   std::vector<ls_node> &all, std::vector<int> &group,
		   std::vector<rtx_insn *> &copies, unsigned n,
		   crp_shared_reload_info *out)
{
  if (all.size () != 2 * n)
    {
      /* Preservation seeds were inserted: the index-mirror mapping
	 below (copy word I at N+I) no longer holds.  */
      crp_sr_refuse (bb, "seeded-row", ~0u);
      return;
    }

  struct sr_cand
  {
    unsigned r;
    std::vector<char> is_def;			/* row index -> group member */
    std::vector<int> epoch_of;			/* row index -> consumer epoch */
    std::vector<unsigned> seg_of;		/* row index -> segment */
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
	 (one fresh constant-only writer plus RMW completions) and
	 consumers, each consumer after its group's last member.  */
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
		  /* Not a pure constant-materialization web.  Named
		     only when groups had already formed (a genuine
		     mixed candidate); a register whose defs are plain
		     computation is simply not a reload web.  */
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
		  /* An RMW completion outside its group would let a
		     consumer read a partial image.  */
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
		  /* First touch is a read (a live-in invariant, or a
		     value from outside the row): not a reload web;
		     the live-in barrier on real webs is named below.  */
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

      /* Copy-half mirror: identical R classification word for word,
	 and byte-identical patterns on every definition-group member
	 (a rename that touched either half's web refuses -- the value
	 identity is the textual-copy fact, re-verified, never
	 assumed).  */
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
	      /* Byte-identity of the materializations, compared on the
		 word's single SET (the loadi patterns carry a scratch
		 clobber, and two SCRATCHes never compare equal).  */
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

      /* CC atoms: no participation, and no atom may span an epoch
	 boundary (the merge interleaves at segment granularity).  */
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

      /* Cross-half interference: the merge moves every surviving
	 copy-half word of segment S ahead of every first-half word of
	 a LATER segment; each such reordered pair must interact
	 through no register but R.  */
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

  /* Modeled gate: the deduplicated candidate must not exceed the
     duplicated candidate's steady-state II (the merge tightens the
     cross-half coupling; a shape where lockstep costs more than the
     removed words buy keeps the duplicated pairing).  */
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

  /* Fill the value oracle, then verify the merged model's own
     candidate before mutating anything.  */
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

  /* Commit: delete the copy half's definition words (this
     transaction's own copies -- any later whole-pairing refusal
     restores the original single row exactly, the deleted words
     included by never having survived), and install the merged
     original order.  */
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

/* ---- Rule-B preservation seeds (-mtt-tensix-optimize-crossrow-pairing-seed)

   The DESIGN-V2 Rule-B rename (round-cc-modulo-evidence-20260825/
   DESIGN-V2.md): a collision web whose fresh root executes INSIDE a
   flat CC atom cannot rename to a dead LREG directly (the predicated
   root writes only enabled lanes, so the dead register's stale
   disabled-lane bits would reach an all-lanes consumer -- the
   crossrow-pairing-rename-cc-domain refusal above).  It CAN rename
   when a typed all-lanes copy F = R (SFPMOV mod-2, the audited
   hidden-state-free assign: rvtt.md rvtt_sfpassign effect audit) is
   seeded immediately after the LAST definition of R that precedes the
   root: in the ambient position before the atom's first CC writer when
   R reaches the atom entry unwritten, or INSIDE the atom directly
   after R's last in-atom definition (e.g. the atom-opening compare
   whose result the predicated root preserves) -- the interior position
   is sound because SFPMOV mod-2 writes every lane REGARDLESS of the CC
   state, and the seed joins the atom's indivisible item so the
   original words keep their interior order and CC contexts:

     - at the seed, F receives R's complete lane image exactly as it
       stands (whatever mix of earlier all-lanes and predicated writes
       produced it -- the copy is a semantic identity on all 32 lanes);
     - between the seed and the fresh root neither R nor F changes
       (F is untouched by every row word -- the free-register search
       invariant -- and the seed sits after R's last preceding
       definition by placement), so F == R lane by lane at the root;
     - the root then writes the same enabled lanes it originally wrote
       into R, and the disabled lanes of F carry exactly the value the
       disabled lanes of R carried -- including a read-modify-write
       root, whose implicit read now consumes the lane-equal F;
     - every later member of the original web, across all later CC
       domains, is rewritten to F until the web's fresh terminator, so
       the equality is inductive and an all-lanes store observes the
       identical value.

   Each seed is one real issued word: it enters the node vector at its
   sequential position and is charged by the same steady-state II model
   and capture budget as every row word.  The seed set commits only on
   a STRICT modeled II improvement over the unseeded (Rule-A) candidate;
   a forward pass may accept an II-neutral seed only as an enabler, and
   the tail of accepted seeds after the last strict improvement rolls
   back (no rider seeds: every emitted seed either strictly improves the
   modeled II or enables a later seed that does).  Everything unproven
   refuses by name and keeps the unseeded pairing byte-identically.  */

/* Dump helper for the seed phase's named refusals.  */

static void
crp_seed_refuse (basic_block bb, const char *why, unsigned r, rtx_insn *insn)
{
  rvtt_refusal_fire_composed ("crossrow-pairing-seed", why);
  if (dump_file)
    fprintf (dump_file, "Crossrow pairing seed refused: "
	     "crossrow-pairing-seed-%s reg %u uid=%d in bb %d\n",
	     why, r, insn ? INSN_UID (insn) : -1, bb->index);
}

/* Rename the web rooted at node I (register R -> F, members I inclusive
   through EXTENT_END exclusive: every node referencing R, through RMW
   redefinitions) as one recorded transaction.  Mirrors the web-member
   rewrite of ls_cyclic_rename_collisions.  */

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

/* Undo exactly the LAST recorded rename (the seed phase's per-candidate
   rollback; each web's F was untouched elsewhere, so F -> R over the
   web is unambiguous).  */

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

/* The transform proper.  Returns true when the pairing committed.  */

static bool
crp_pair_loop (basic_block bb, std::vector<basic_block> &visited)
{
  crp_loop lp;
  if (!crp_admit_loop (bb, &lp))
    return false;

  unsigned n = lp.nodes.size ();

  /* Phase 2a: emit row B as a textual copy of row A, after A's last
     word and before the separator.  Every later refusal deletes the
     copies, leaving the original stream byte-identical (A's own words
     are never mutated before the commit point).  */
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

  /* Phase 2b: rebase row B's Dst accesses to the second row of the
     shared counter frame.  */
  crp_queue_dst_rebase (copies[lp.load], lp.dst_addr + lp.dst_step);
  crp_queue_dst_rebase (copies[lp.store], lp.dst_addr + lp.dst_step);
  if (!apply_change_group ())
    {
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-dst-rebase-constraint");
    }
  df_insn_rescan (copies[lp.load]);
  df_insn_rescan (copies[lp.store]);

  /* Phase 2c: node vectors over the doubled row.  The copies re-admit
     through the same vocabulary (they are textual copies with typed
     constant rewrites); a failure here is fail-closed, not an ICE.  */
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

  /* Baseline: the two logical iterations in their original sequential
     order (exactly the stream the doubled loop would execute), judged
     by the same cyclic steady-state model as the candidate.  Measured
     BEFORE any rename.  */
  std::vector<int> base_order (2 * n);
  for (unsigned i = 0; i != 2 * n; ++i)
    base_order[i] = i;
  int base_ii = ls_cyclic_ii (all, base_order);
  unsigned pads_before = ls_pad_sites (visited, bb, all);

  /* Phase 2d: break allocator-packed false recurrences; fresh webs may
     root only in the ambient all-lanes state (never inside an atom).  */
  std::vector<bool> start_allowed (2 * n, true);
  for (unsigned half = 0; half != 2; ++half)
    for (const auto &atom : lp.atoms)
      for (unsigned i = atom.first; i <= atom.second; ++i)
	start_allowed[half * n + i] = false;
  std::vector<ls_rename> renames;
  /* Under the stall-words extension the copy half's webs take the free
     LREGs first: breaking row-B serialization is the pairing's whole
     benefit, and an intra-row false-recurrence rename must not starve
     it (the tanh anatomy: three free LREGs, three row-B webs, and the
     row-A loadi WAW web grabbing one leaves the row-B accumulator
     serialized -- II gate refuses and the transform dies).  */
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

  /* Item grouping over the doubled row: every atom instance is one
     indivisible super-item id; pure words (and any later preservation
     seeds) are -1 singletons.  */
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

  /* Modeled steady-state II of the current candidate order (INT_MAX on
     a construction failure -- the caller's belts refuse).  */
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

  /* Phase 2d': Rule-B preservation seeds (sub-flag; see the header
     comment above crp_seed_refuse).  Fail-closed: any refusal keeps
     the unseeded Rule-A state exactly.  */
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
      /* One entry per accepted Rule-B rename, parallel to the tail of
	 RENAMES: the emitted seed word, or null for a full-lane root
	 (a bare all-lanes copy needs no preservation seed -- it writes
	 every lane itself, so the fresh register never exposes dead
	 bits; DESIGN-V2 Rule A carried into the atom interior by the
	 mod-2 lane-immunity fact).  */
      std::vector<rtx_insn *> commits;
      bool progress = rule_a_ii != INT_MAX;
      while (progress)
	{
	  progress = false;
	  for (unsigned i = 0; i != all.size () && !progress; ++i)
	    {
	      if (group[i] < 0)
		continue;	/* Rule-B roots live inside atoms */
	      unsigned af = i;
	      while (af && group[af - 1] == group[i])
		--af;
	      if (af == i)
		continue;	/* the atom-opening CC writer roots no
				   Rule-B web (no ambient point between
				   it and the seed would separate them) */
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
		  /* A FULL-LANE root needs no seed: the bare all-lanes
		     copy (SFPMOV mod-2, the audited full-copy-semantics
		     spill vocabulary) writes every lane regardless of
		     the CC state, so the fresh register carries the
		     complete value from the root on and no disabled
		     lane can expose dead bits (DESIGN-V2 Rule A,
		     carried into the atom interior by the mod-2
		     lane-immunity fact).  */
		  bool full_lane_root = bare_lreg_copy_p (all[i].insn)
		    && !TEST_HARD_REG_BIT (all[i].regs.uses, r);
		  /* Seed placement (predicated roots): the root must
		     observe exactly the value the seed captured, so the
		     seed sits after the LAST definition of R that
		     precedes the root -- in the ambient position
		     immediately before the atom when R reaches the atom
		     entry unwritten, or INSIDE the atom immediately
		     after R's last in-atom definition (e.g. the
		     atom-opening compare that produces the value the
		     predicated root preserves).  The interior position
		     is sound because the seed word itself is lane-
		     immune -- the bare-SET SFPMOV mod-2 writes every
		     lane regardless of the CC state (the audited
		     hidden-state-free fact) -- and it joins the atom's
		     indivisible item, so the original words' interior
		     order and CC contexts are untouched.  */
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
		  /* Web extent: through RMW redefinitions, exclusive
		     before the next fresh writer (the established web
		     discipline).  */
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
		     replay capture buffer (mirror of the admission
		     bound), or the counted-loop capture downstream
		     stops firing.  */
		  if (!full_lane_root
		      && all.size () + 1 > (unsigned) XTT_DELIVERY_CAPTURE_SLOTS)
		    {
		      crp_seed_refuse (bb, "capture-budget", r, all[i].insn);
		      continue;
		    }
		  /* Emit the all-lanes preservation copy at the chosen
		     position (predicated roots only), then re-admit it
		     through the row vocabulary.  */
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
		      /* The charged seed does not pay here (an
			 II-neutral commit is retained only as a possible
			 enabler; a worse one never).  */
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
      /* No rider commits: roll back everything after the last STRICT
	 modeled improvement (all of it when nothing improved).  */
      if (commits.size () > strict_commits)
	{
	  rvtt_refuse (RVTT_REF_CROSSROW_PAIRING_SEED_NO_II_IMPROVEMENT, dump_file,
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

  /* Phase 2d'': shared-reload dedupe (sub-flag; see the header comment
     above crp_shared_reload).  Fail-closed both ways: any admission
     refusal keeps the duplicated pairing exactly, and a committed
     dedupe that any LATER belt refuses abandons the whole pairing
     transaction -- the deleted words were this transaction's own
     copies, so the restore paths below still return the original
     single row byte-identically.  */
  crp_shared_reload_info shared_reload;
  if (riscv_tt_opt_crossrow_shared_reload > 0)
    crp_shared_reload (bb, lp, all, group, copies, n, &shared_reload);

  /* Phase 2e: candidate order -- the dependence-legal global item
     schedule (atoms indivisible, unrenamed shared webs serialize).  */
  std::vector<rtx_insn *> candidate = crp_candidate_order (all, group);
  if (!crp_order_legal_p (all, candidate))
    {
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-order-hazard");
    }
  if (!crp_shared_reload_order_sound_p (shared_reload, candidate))
    {
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
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      if (dump_file)
	fprintf (dump_file, "Crossrow pairing refused: no modeled "
		 "steady-state II decrease in bb %d (%d -> %d)\n",
		 bb->index, base_ii, cand_ii);
      return false;
    }

  /* Phase 2f: exact-restore record, then commit the order.  */
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
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-pad-site-increase");
    }
  /* Capture-overflow belt (stall-words extension): the doubled record
     the downstream counted-loop capture will see is every row word
     PLUS every pad the nop inserter still owes the final order; at
     2n == XTT_DELIVERY_CAPTURE_SLOTS a single surviving pad site
     silently trades the record-plus-launch delivery for a rolled
     issue stream (the adjudicated round-cc-modulo profitability
     defect).  Refuse rather than roll.  */
  if (riscv_tt_opt_crossrow_pairing_stall_words > 0
      && all.size () + pads_after > (unsigned) XTT_DELIVERY_CAPTURE_SLOTS)
    {
      crp_restore_chain ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-capture-overflow");
    }

  /* Phase 2g: shared control rewrite -- the separator advances both
     rows at once and the countdown halves.  */
  extract_insn (lp.separator);
  validate_change (lp.separator, recog_data.operand_loc[1],
		   GEN_INT (2 * lp.dst_step), true);
  rtx cset = single_set (lp.counter);
  validate_change (lp.counter, &XEXP (SET_SRC (cset), 1), GEN_INT (-2),
		   true);
  if (!apply_change_group ())
    {
      crp_restore_chain ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-control-rewrite-constraint");
    }
  df_insn_rescan (lp.separator);
  df_insn_rescan (lp.counter);

  /* Post-commit belt: the doubled separator must still derive as the
     typed row step at exactly twice the advance.  */
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
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-row-step-shape",
			 lp.separator);
    }

  if (dump_file)
    fprintf (dump_file, "Crossrow pairing: bb %d rows=2 nodes=%zu "
	     "II %d -> %d renames=%zu seeds=%zu dst-addr=%ld/%ld "
	     "step=%ld->%ld trips=%ld->%ld target=bh\n",
	     bb->index, all.size (), base_ii, cand_ii, renames.size (),
	     seed_insns.size (),
	     (long) lp.dst_addr, (long) (lp.dst_addr + lp.dst_step),
	     (long) lp.dst_step, (long) (2 * lp.dst_step),
	     (long) lp.trips, (long) (lp.trips / 2));
  return true;
}

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

/* ---- Cyclic-interior region scheduling (lane IJ, default off) ----

   -mtt-tensix-optimize-cyclic-region-schedule lifts the self-loop
   deferral for the MULTI-REGION row shape the one-region cyclic
   extension refuses (round-interleave-seam-barrier-word /
   -row-not-one-region): a row chopped by issued barrier words (CC
   writes, Dst traffic, config accesses) into several straight-line
   regions.  Every barrier word keeps its position; each INTERIOR
   region is re-list-scheduled under the established region vocabulary
   (admission, entry pins, deterministic list order), and the candidate
   commits only on a STRICT decrease of the WHOLE ROW's modeled
   steady-state initiation interval -- the wrapped cyclic issue model
   (ls_cyclic_ii) over EVERY issued word of the block, with unaudited
   latencies floored at ZERO identically in baseline and candidate (a
   modeled lower bound, never a claimed cycle count: only the strict
   decrease is acted on, and a floored latency can only hide a stall
   both orders share).  The linear boundary model that motivated the
   self-loop deferral is never consulted for acceptance.

   SOUNDNESS (why a within-region reorder is cyclically bit-exact):
   region members move only relative to each other; barrier words and
   region boundaries are fixed.  A dependence between two ITERATIONS
   either involves a fixed word, or connects iteration i's instance of
   the region to iteration i+1's instance -- and in the concatenated
   stream every word of the earlier instance precedes every word of
   the later one regardless of the interior permutation.  Dependences
   WITHIN one iteration are the region DAG's, honored by the list
   order exactly as in the straight-line case (the same fail-closed
   ls_dependence vocabulary; predicated RMW uses include defs).

   Refusals by name (original order kept byte-identically):
     cyclic-interior-opaque-word     raw asm / opaque effects in the row
     cyclic-interior-backedge-seam   region contains the row's first or
				     last issued word (the boundary the
				     deferral exists for)
     cyclic-interior-repeated-shape  region signature repeats in the row
				     (replay/MOP re-roll owns copy
				     isomorphism)
     cyclic-interior-no-ii-decrease  candidate II >= current II
   plus the pad-site / entry-pad-flip commit guards of the
   straight-line scheduler (the WH correctness carrier).  The row-level
   replay-owner and call refusals are the caller's.  */

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
	continue;		/* scalar control / ghost: no word */
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
	nd.lat = 0;		/* model floor, charged both sides */
      nd.words = get_attr_length (insn) / 4;
      if (e.next_slot_stall)
	/* The architectural acceptance stall is an issue fact: one
	   extra slot per occurrence (the crossrow pairing's priced
	   rule), identical in baseline and candidate.  */
	nd.words += 1;
      if (!collect_sfpu_regs (insn, &nd.regs))
	/* Defless CC/store words: keep their real LREG uses for RAW
	   ordering (position fixed anyway -- they are never region
	   members).  */
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

  for (unsigned ri = 0; ri != regions.size (); ++ri)
    {
      ls_region &r = regions[ri];
      const unsigned n = r.nodes.size ();

      /* Replay/MOP re-roll isomorphism: repeated shapes defer.  */
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

      /* Locate the region inside the body model: admitted nodes are
	 consecutive issued words (only barrier words separate
	 regions), so the run is contiguous.  */
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

      /* Interior only: a region containing the row's first or last
	 issued word sits on the backedge seam this pass never
	 models.  */
      if (first == 0 || first + n == bn)
	{
	  rvtt_refuse (RVTT_REF_CYCLIC_INTERIOR_BACKEDGE_SEAM, dump_file,
		       "List-schedule (cyclic-interior) refused: "
		       "cyclic-interior-backedge-seam at uid=%d in bb %d\n",
		       INSN_UID (r.nodes[0].insn), bb->index);
	  continue;
	}

      /* Entry pins: the straight-line scheduler's own discipline over
	 the region's acyclic baseline (candidate construction only;
	 acceptance is the cyclic model below).  */
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

      /* Candidate: the deterministic list order, judged on the WHOLE
	 row's steady-state II.  */
      std::vector<int> order = ls_list_order (r.nodes);
      std::vector<int> cand_body (body_order);
      for (unsigned k = 0; k != n; ++k)
	cand_body[first + k] = (int) (first + order[k]);
      int cand_ii = ls_cyclic_ii (body, cand_body);
      if (cand_ii >= cur_ii)
	{
	  rvtt_refuse (RVTT_REF_CYCLIC_INTERIOR_NO_II_DECREASE, dump_file,
		       "List-schedule (cyclic-interior) refused: "
		       "cyclic-interior-no-ii-decrease at uid=%d in bb %d "
		       "(%d -> %d)\n",
		       INSN_UID (r.nodes[0].insn), bb->index, cur_ii,
		       cand_ii);
	  continue;
	}

      /* Commit guards: the nop inserter's pad-site probe (the WH
	 correctness carrier) and the entry producer's pad state, as
	 in the straight-line scheduler.  */
      unsigned pads_before = ls_pad_sites (visited, bb, r.nodes);
      bool ep_dynamic
	= r.entry_producer
	  && get_attr_xtt_delay (r.entry_producer) == XTT_DELAY_DYNAMIC;
      bool ep_needed_before
	= ep_dynamic
	  && delay_nop_needed_p (visited, bb, r.entry_producer,
				 XTT_DELAY_DYNAMIC);

      /* Exact-restore record, debug insns included.  */
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
	  fprintf (dump_file, "List-schedule (cyclic-interior): bb %d "
		   "region at uid=%d nodes=%u row II %d -> %d "
		   "target=%s\n",
		   bb->index, INSN_UID (r.nodes[0].insn), n, cur_ii,
		   cand_ii, TARGET_XTT_TENSIX_WH ? "wh" : "bh");
	  for (unsigned k = 0; k != n; ++k)
	    fprintf (dump_file, "List-schedule slot-order=%u uid=%d\n",
		     k, INSN_UID (r.nodes[order[k]].insn));
	}
      body_order = cand_body;
      cur_ii = cand_ii;
    }
}

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
      /* A self-loop row executes back-to-back across the backedge (and,
	 captured, across every playback): the row is a cycle, and this
	 scheduler's linear boundary model mispredicts the seam.  The
	 cyclic adjacency is capture rotation's audited territory.  */
      bool self_loop = false;
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (e->dest == bb)
	  self_loop = true;
      if (self_loop && !riscv_tt_opt_round_interleave
	  && !riscv_tt_opt_cyclic_region_schedule)
	{
	  if (dump_file)
	    fprintf (dump_file, "List-schedule deferred: cyclic row "
		     "adjacency in bb %d (capture rotation owns the "
		     "backedge seam)\n", bb->index);
	  continue;
	}

      /* Phase 1: collect the block's candidate regions.  */
      std::vector<ls_region> regions;
      std::vector<ls_node> nodes;
      rtx_insn *anchor = nullptr;
      rtx_insn *entry_producer = nullptr;
      HARD_REG_SET region_unaudited;
      CLEAR_HARD_REG_SET (region_unaudited);
      bool stop_block = false;
      unsigned tensix_barriers = 0;	/* issued Tensix words outside
					   any region (seam hazards for
					   the cyclic extension) */
      bool bb_has_call = false;

      auto flush = [&] ()
      {
	/* Interleaving needs a third participant: a two-node region is
	   either order-forced (dependent) or model-symmetric under the
	   interior objective, so regions below three nodes are skipped
	   by name rather than scheduled.  */
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
		  /* The audited entry horizon is exactly ONE issued
		     instruction deep: every admitted latency is <= 1,
		     so a producer two issue slots back has an expired
		     shadow.  An entry producer whose latency is
		     unaudited (or beyond the window) contributes its
		     defs as the pin hazard instead of a modeled floor.
		     Unknown-latency producers deeper than the entry
		     adjacency are unmodeled in baseline and candidate
		     alike -- the same exposure the fill phases carry
		     when a filler moves toward them.  Zero-length
		     interface markers are not producers: they deliver
		     no word and stage no hardware event.  */
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
	    /* Raw assembly may deliver Tensix words the effect
	       vocabulary cannot see: a seam hazard for the cyclic
	       extension (the straight-line phases already never move
	       anything across it).  */
	    ++tensix_barriers;
	  if (CALL_P (insn))
	    bb_has_call = true;
	  flush ();
	  if (GET_CODE (insn) == INSN && recog_memoized (insn) >= 0
	      && get_attr_type (insn) == TYPE_TENSIX
	      && get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
	    stop_block = true;	/* established capture discipline */
	}
      if (!stop_block)
	flush ();

      /* Round-interleave cyclic extension: a self-loop row whose
	 admitted nodes are its ONE region and whose block carries no
	 other issued/foreign Tensix word, no replay owner, and no call
	 schedules under the wrapped steady-state II model; every other
	 self-loop shape keeps the deferral, by name.  */
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
	  if (!why_c && riscv_tt_opt_round_interleave)
	    {
	      ls_schedule_region_cyclic (bb, regions[0].nodes,
					 regions[0].anchor, visited);
	      continue;
	    }
	  /* Cyclic-interior extension: the multi-region self-loop
	     shapes the one-region path refuses -- and, when the
	     round-interleave flag is off, every self-loop shape --
	     schedule INTERIOR regions under the whole-row cyclic II
	     acceptance.  The replay-owner and call refusals stand
	     (an owner's capture discipline and a call's foreign words
	     are outside the row model).  */
	  if (riscv_tt_opt_cyclic_region_schedule
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

      /* Phase 2: repeated region shapes defer by name -- unrolled row
	 copies must stay textually isomorphic for the replay former's
	 re-roll and the MOP re-roll, and boundary-context differences
	 would schedule sibling copies differently.  Under the
	 round-interleave flag, EXACTLY TWO isomorphic copies schedule
	 as a pair under one shared permutation (isomorphism preserved;
	 see ls_schedule_iso_pair); larger families keep the deferral.  */
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
	    continue;	/* round-interleave alone owns no single region */
	  ls_schedule_region (bb, regions[i].nodes, regions[i].anchor,
			      regions[i].entry_producer,
			      next_issued_insn
				(bb, regions[i].nodes.back ().insn),
			      regions[i].unaudited_defs, visited);
	}
    }
}

/* ---- Capture rotation: cross-row interlock fill ----

   fill_interlock_shadows above fills a modeled transparent stall with an
   independent instruction found later in the SAME row.  Inside a counted
   row loop that replay formation captures and the launch unroll plays
   back-to-back, the row is a CYCLE: the capture's last instruction is
   issued immediately before the next playback's first, so independent
   members also exist across the row boundary.  This phase performs the
   two provable cyclic reorders of that cycle:

   - SEAM FILL (no boundary work): when the row's last issued word has an
     audited one-slot result latency and the row's first word consumes
     one of its destinations (a loop-carried dependence that becomes a
     back-to-back stall in the launch run), a provably independent row
     member moves to the row's tail (or head), separating the pair.  The
     move stays inside one iteration -- a plain reorder, per-row
     semantics untouched, no prologue or epilogue.  The seam is this
     phase's own territory even where the DYNAMIC delay probe fires (WH):
     no in-row mechanism reaches across the backedge, and a committed
     move must prove the probe is quiet afterwards.

   - PROLOGUE ROTATION (iteration-shifted): a filler whose inputs are all
     loop-invariant (an immediate load, a copy from an invariant
     register) may move FORWARD past its own consumers into a stalled
     gap: after the move, consumers between the old and new position read
     the previous iteration's instance -- the same value, because the
     filler's inputs never change inside the row and nothing else writes
     its destination.  The run's FIRST row has no previous instance, so
     an explicit prologue copy of the filler is emitted in the loop's
     dedicated preheader; the FINAL row needs no epilogue because the
     relocated instance still executes within its own iteration and
     leaves the same final value.  Proof obligations, refusing by name:
       . the filler's effects are audited-clean: no CC write, no
	 configuration access, no RWC step, no Dst traffic (the bare
	 unpredicated copy is exempt as established), and an audited
	 result latency of zero;
       . a lane-predicated (CC-reading) filler is admitted only when
	 every row member provably writes no CC: the CC state is then
	 constant across the whole launch run, so the filler writes the
	 same lanes with the same values on every trip and the prologue
	 copy (executing under the loop-entry CC state) covers the first
	 row exactly; the all-lanes bare copy needs no such proof;
       . every input register is invariant in the row (no writer),
	 which also excludes read-modify-write forms;
       . the filler is its destination's only writer in the row;
       . no row member before the filler reads the destination: such a
	 read consumes a value carried across the row boundary that the
	 prologue would change (the entry-boundary dependency);
       . the destination is not live into the row header;
       . every crossed instruction satisfies the established crossing
	 discipline (shadow_crossing_safe_p);
       . the loop has a dedicated preheader to hold the prologue.

   Rotation never adds a row word (the prologue copy is one delivered
   word per RUN, outside the capture), so under the corrected delivery
   model any strictly decreasing modeled cyclic stall count wins.  The
   accounting covers every adjacency a move changes, including the
   vacated position and the row-boundary adjacency; any term depending on
   an unaudited latency refuses byte-identically.  Required-nop sites
   INSIDE the row stay owned by the nop inserter and fill_nop_shadows.

   Admission is the capturable-row shape mirroring the counted-loop
   replay hoist: a single-BB counted loop whose payload is replay-safe
   Tensix words, optionally one trailing typed TTINCRWC, one scalar
   counter step after the payload, and the final conditional jump.
   Everything else refuses by name.  Purely structural: no operation
   identity, opcode calendar, coefficient value, or instruction-word
   fingerprint participates.  */

struct rotation_row
{
  basic_block bb;
  std::vector<rtx_insn *> issued; // issued Tensix words, in order
};

/* The non-self predecessor of self-loop BB when it is a dedicated
   preheader (single successor, no abnormal edge), else null.  */

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

/* Admission: BB is a self-loop with the capturable-row shape.  Returns
   false with *REASON naming the refusal when BB is a self-loop that
   fails the shape; *REASON stays null when BB is not a self-loop.  */

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
	    continue; // bookkeeping ghost
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
      /* Scalar RISC insn: admit exactly the loop's counter step.  */
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

static const char *
rotation_target_name ()
{
  return TARGET_XTT_TENSIX_WH ? "wh" : "bh";
}

/* Crossing walk shared by both movers: every issued Tensix insn in
   [FROM, TO] must satisfy the crossing discipline for a filler of class
   HIDDEN_FREE, and ghost/marker register references join *CROSSED.
   Returns the offending insn, or null when the segment is crossable.  */

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

/* ---- Plain-reorder filler pool widening (lane DL, D3 follow-up) ----

   The plain-reorder movers (seam fill and the interior gap fill below)
   change only the within-iteration issue order; no prologue copy ever
   executes outside the row.  For those movers the filler pool extends
   beyond the pure-LREG classes to two audited hidden-state classes,
   each admissible only when every crossed word is proven inert to the
   state the filler carries:

   - an audited Dst-touching word (the load/store subunits with an
     audited result latency and no RWC step of its own): legal to move
     iff no crossed word touches Dst or steps an RWC counter -- then
     the filler reads/writes the identical Dst rows at its new
     position, and every other word's Dst view is unchanged;
   - the typed row-step word (TTINCRWC): legal to move iff no crossed
     word touches Dst or the RWC state -- the counter step commutes
     with pure-LREG words.  While replay-hoist is enabled this class
     DEFERS by name: counted_loop_payload (rtl-rvtt-replay.cc) refuses
     any loop whose TTINCRWC is not the trailing word, so moving it
     inward would trade a whole capture for one issue slot.

   The prologue mover keeps the pure-LREG pool unchanged: its prologue
   copy executes once outside the row, which is only sound for the
   row-invariant values that pool guarantees -- a Dst access or an RWC
   step executed twice is not.

   Everything else refuses by name.  Purely structural: no operation
   identity, opcode calendar, coefficient value, or instruction-word
   fingerprint participates.  */

enum rotation_filler_kind
{
  ROT_FILLER_REFUSED,
  ROT_FILLER_LREG,	/* the established shadow_filler_p classes */
  ROT_FILLER_DST,	/* audited Dst-touching word, RWC-neutral */
  ROT_FILLER_RWC_STEP	/* the typed row-step word */
};

/* Classify INSN for the plain-reorder movers.  *WHY names the refusal
   for the widened classes; the established pure-LREG refusals stay
   silent exactly as before (byte-identical dump behavior on rows the
   widening does not reach).  */

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
  /* Every register reference must be an SFPU register (a scalar
     reference carries dependences this pool does not track), but unlike
     collect_sfpu_regs the widened classes need no LREG destination:
     stores and the row-step word define nothing.  */
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
  /* Read-modify-write conservatism for CC-predicated lane writes.  */
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

/* A crossed word a Dst-touching or RWC-stepping filler may pass:
   effects on record, no CC write (the filler's lane predicate and the
   row's CC constancy), and no Dst, RWC, or memory interaction of its
   own.  Fail-closed: a Dst-reading filler does not even cross another
   Dst reader.  */

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

/* Kind-aware crossing walk: the established discipline for the
   pure-LREG classes, the Dst/RWC-inert proof for the widened ones.  */

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

/* Post-move required-nop guards, exactly fill_nop_shadows' discipline:
   the committed order must not manufacture a new DYNAMIC-delay pad site
   at the producer, the filler, or the filler's old predecessor.  */

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

/* Close the row's seam stall (last issued word -> first word of the next
   playback) by moving one provably independent member to the tail or the
   head.  A plain within-iteration reorder: no prologue or epilogue.  */

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
  /* A STATIC delay pads before any non-nop word: no filler can close it,
     and a pad materializing at the tail would eat the closure.  */
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
	    /* A STATIC-delay filler drags its pad into the seam slot.  */
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

/* Close a modeled in-row stall by moving one provably independent row
   member into the gap -- a plain within-iteration reorder exactly like
   the seam fill, extended to interior gaps and the widened filler
   classes (lane DL).  Runs after the seam and prologue movers, so the
   established fire shapes keep their movers byte-identically.
   Adjacency accounting is cyclic: the row replays, so a candidate at
   either row end trades against the seam adjacency.  */

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
      /* Required-nop sites inside the row stay owned by the nop
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
	  /* A STATIC-delay filler drags its pad into the vacated slot.  */
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

	  /* Cyclic vacated-position exchange.  */
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

/* Close a modeled stall (in-row or seam) by rotating an invariant-input
   member forward past its own consumers, with a prologue copy in the
   dedicated preheader covering the run's first row.  */

static bool
rotate_prologue_fill (rotation_row const &row, basic_block preheader,
		      std::vector<basic_block> &visited)
{
  auto const &issued = row.issued;
  unsigned m = issued.size ();

  /* Row-wide register write set: invariance and single-writer proofs.  */
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

  /* CC constancy across the run: every issued row member provably writes
     no CC.  Lane-predicated fillers are admissible exactly then.  */
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
      /* Required-nop sites inside the row stay owned by the nop inserter
	 and fill_nop_shadows; the seam has no in-row owner.  */
      if (!seam
	  && get_attr_xtt_delay (producer) == XTT_DELAY_DYNAMIC
	  && delay_nop_needed_p (visited, row.bb, producer,
				 XTT_DELAY_DYNAMIC))
	continue;
      if (dump_file)
	fprintf (dump_file, "Capture rotation: modeled stall after uid=%d "
		 "in bb %d\n", INSN_UID (producer), row.bb->index);

      /* Forward moves only: candidates strictly before the gap; for the
	 seam, any interior member (the first word is the consumer).  */
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
	  /* The prologue copy must be a plain single-SET word: a copied
	     hard-register clobber could land on live preheader state.  */
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

	  /* Single writer, and no reader before the filler (an entry
	     boundary dependency the prologue cannot honor).  */
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
// instructions.

static void
transform (function *fn)
{
  std::vector<basic_block> visited;

  basic_block bb;
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
	    continue;

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
      /* Before the region schedulers: a committed pairing leaves a
	 doubled self-loop row whose pure spans the later phases may
	 still improve; a refusal leaves the stream byte-identical.  */
      crossrow_pair_rows (fn);
    if (riscv_tt_opt_list_schedule || riscv_tt_opt_round_interleave
	|| riscv_tt_opt_cyclic_region_schedule)
      /* The round-interleave flag enables only the cyclic self-loop
	 and isomorphic-pair extensions inside; the cyclic-interior
	 flag only the multi-region self-loop extension; single
	 straight-line regions still require the list-schedule flag.  */
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
}; // class pass_rvtt_schedule

} // anon namespace

rtl_opt_pass *
make_pass_rvtt_schedule (gcc::context *ctxt)
{
  return new pass_rvtt_schedule (ctxt);
}

/* ----------------------------------------------------------------------
   Drain-aware boundary placement (macro-planner emission service, under
   -mtt-tensix-optimize-drain-schedule; placement logic lives here, the
   planner's emission consumes the verdict).

   The planner derives core_drain_slots -- the greatest event writeback
   distance past a run's last issue slot -- from the descriptor's own
   SequenceBits delay fields (rvtt-macro-sched-core.h, core_drain_slots;
   the delays are the derived timing calendars, never hand constants) and
   emits that many SFPNOPs after EVERY formed run.  A region split into
   several runs by an architectural boundary instruction therefore
   executes the drain once per run where the architecture requires it
   only before the first genuinely conflicting follower access.

   Architectural basis (the corrected simulator's adjudicated retirement
   semantics, craq-sim 9f324140 src/tensix.cpp:9820-9945, pinned to the
   BlackholeA0 SFPLOADMACRO functional spec; recorded in the
   pre-registered design ~/sfpi-uplift/drain-study-20260818):

     Launch-latched state (safe to mutate while events are in flight):
       L1  the store event's Dst row (dst_rwc + imm10 + DEST_TARGET
	   offset are read AT LAUNCH, tensix.cpp:9848-9853, 9905);
       L2  the store format (Misc/launch Mod0, resolved at launch :9907);
       L3  the Dst layout the format decode depends on (:9913-9917).
     Live-at-execution state (mutation inside the event horizon races):
       E1  the lane predicate (live lane-enable evaluation at execution,
	   :9908-9911) -- any CC write inside the horizon changes which
	   lanes an in-flight event touches;
       E2  LReg contents read or written by staged events (events execute
	   against live architectural state via the ordinary
	   per-instruction executors, :9830-9833);
       E3  the LoadMacroConfig state itself (templates/sequence/misc);
       E4  Dst rows an in-flight store writes (a read before writeback is
	   a RAW hazard).
     Horizon arithmetic:
       H1  event writeback slot = carrier issue slot + programmed delay
	   (the exact model core_drain_slots already uses; a run's last
	   pending writeback is therefore at most drain_slots past its
	   final issue slot);
       H2  at most one instruction issues per cycle, so the position of a
	   follower word in the issue stream is a LOWER bound on its
	   issue-cycle distance from the boundary -- counting stream
	   slots is sound, and any dynamic stall only moves follower
	   accesses later (the safe direction, since every proof below is
	   "follower access strictly after pending writeback").

   The verdict is per boundary: the drain of run K may be elided exactly
   when (a) everything between run K and run K+1 is a discovery-admitted
   pure-RWC run separator -- launch-latched state only (L1-L3) -- whose
   issued words moreover provably survive to the final stream (the
   dst-autoincr AIC_RWC_STEP contract: FACE-class typed advances and the
   audited raw SETRWC-class words separate rows and are never absorbed;
   INC-class TTINCRWC is absorbable and earns no slot credit), and
   (b) every event of run K+1 that touches any architectural state first
   executes strictly after the last pending writeback, by the derived
   slot arithmetic above, and (c) the enumerated follower words cover the
   whole horizon.  Anything unprovable refuses by name and keeps the full
   derived drain byte-identically.  The final run's drain -- the region's
   exit contract (a formed function or loop body must not hand events in
   flight to an invisible follower stream) -- is never elided; that is
   the caller's obligation, not checked here.  */

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

/* Region members are deleted (or re-emitted inside the formed calendar)
   at formation; the boundary walk skips them.  */

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

static bool
drain_run_separator_p (const macro_region &region, rtx_insn *insn)
{
  for (rtx_insn *sep : region.run_separators)
    if (sep == insn)
      return true;
  return false;
}

/* Stable refusal name of a follower-event conflict, by effect class
   (E4, E1, E3, E2 in that order of specificity).  */

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

/* Decoded pending-event horizon of one emitted run, shared by the
   intra-region boundary proof and the loop-backedge proof below.  All
   fields derive from the descriptor's own SequenceBits delays and the
   adopted schedule's slot assignment -- derived timing calendars,
   never hand constants.  */

struct drain_horizon
{
  int max_dist;			/* == desc.drain_slots, cross-checked  */
  unsigned words_per_row;
  int carrier_pos[8];
  auto_vec<int> word_pos;	/* per schedule event: row position    */
  struct { unsigned kind; unsigned delay; } events[8][4];
  int n_events[8];
};

/* Decode the run horizon from SCHEDULE and DESC into *H.  Word
   positions mirror emit_planner_run's emission order (issue slots
   ascending).  Per-macro launched-event timing is decoded from the
   descriptor's OWN sequence words through the established SequenceBits
   format (rvtt-macro-tables.h: byte i programs sub-unit i; case = bits
   2:0, the event executes at issue + 1 + delay(bits 5:3); provenance
   docs/TIMING_CALENDAR_DERIVATION.md 1-2).  The frozen whole-word
   programs left per-event delays untranscribed in the schedule
   (DELAY_UNKNOWN), but the words themselves carry them -- decoding
   the emitted words is the one derivation that can never drift from
   what the hardware sequences.  Case 1 is architecturally undefined;
   SKIP/NOP bytes stage no architectural event.  The decoded pending
   horizon is cross-checked against the descriptor's own drain_slots
   -- the two derive from the same calendar, so a mismatch means the
   timing facts are not established for this shape and the boundary
   refuses.  */

/* REQUIRE_EXACT selects the establishment rule.  The intra-region
   boundary proof (the lane-AY envelope) requires the decoded pending
   horizon to EQUAL the descriptor's emitted drain_slots -- byte-stable
   with the shipped behavior.  The loop-backedge proof admits a
   conservative emitted drain (a frozen proven-calendar figure may
   exceed the decoded truth -- the compact CC program carries 3 where
   the SequenceBits pend only 1); the decoded words are the derivation
   that can never drift from what the hardware sequences, so the proof
   horizon is the DECODED pending, refused if it ever exceeded the
   emitted drain.  */

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

  /* Every sequence word's events ride a known carrier; a macro without
     a located carrier word leaves events unaccounted -- refuse.  */
  for (unsigned m = 0; m != desc.n_seq && m != 8; ++m)
    if (h->n_events[m] > 0 && h->carrier_pos[m] < 0)
      return drain_refuse (dump, "drain-delay-unproven", nullptr);

  /* Pending horizon: greatest event-execution distance past the run's
     last issue slot, over the decoded events of the trailing rows
     (event execution = carrier position + 1 + delay; rows are
     words_per_row issue slots apart).  */
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

/* Prove that every architectural access of the follower rows
   [ROW_BEGIN, ROW_END) is ordered after every pending event of the
   horizon H, with SEP_CREDIT proven follower words already issued
   before the first row (H1+H2): a follower word at run position P
   issues no earlier than boundary + sep_credit + 1 + P, its launched
   events execute at issue + 1 + delay, an explicit word's own access
   is counted at issue (the conservative earliest), and a carrier
   word's own front-end VD write is counted at issue too (lane FL,
   FH-4: admitted only under the lane-EV protections -- VD
   alternation, store-only sacrificial VD, the CC-template model's
   next-row obligations).  Ordering at equal
   cycles follows the established transactional model
   (rvtt-macro-tables.h derived-calendar provenance: ISA spec + CRAQ
   generic executor + hand MulInt32 -- "retire-before-issue"): a
   staged event retiring at cycle X retires BEFORE the front-end
   instruction issuing at X executes, so a FRONT-END access at the
   last retirement cycle is ordered after every pending event
   (equality admitted); two staged EVENTS at one cycle stay a race --
   the silicon-adjudicated cc-restore-store-race failure mode -- so
   launched follower events keep the strict inequality.  The
   enumerated follower words must cover the whole horizon.  */

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
	break;			/* every later access clears by time */
      if (region.rows[r].insns.length () != schedule.events.length ())
	return drain_refuse (dump, "drain-follower-opaque", nullptr);
      /* The launch word's OWN front-end VD write (lane FL, FH-4; the
	 lane-EV corruption class at a RUN boundary).  A launch is a
	 front-end instruction: retire-before-issue admits it at the
	 last retirement cycle (equality), but an EARLIER issue writes
	 its VD while the horizon's events -- hosted consumers of the
	 SAME descriptor's previous rows -- are still in flight.  The
	 established protections are exactly the lane-EV predicate
	 (rvtt-macro-desc.h macro_launch_spec): VD alternation (the
	 conservative VD policy's own envelope -- adjacent rows target
	 different registers), a store-only sacrificial VD (written,
	 never read), and the CC-template model's proven next-row
	 obligations (macro_cc_model: store-before-next-def,
	 restore-visibility; silicon-proven multi-row on the unified
	 where kernel).  A fixed-VD VALUE carrier has none of them --
	 the previous run's pending events read the register this
	 launch overwrites -- so the boundary refuses by name.  A
	 carrier with no launch spec on record refuses fail-closed.  */
      for (unsigned m = 0; m != desc.n_seq && m != 8; ++m)
	{
	  if (h.carrier_pos[m] < 0)
	    continue;
	  int issue = (int) base + 1 + h.carrier_pos[m];
	  if (issue >= max_dist)
	    continue;		/* front-end: equality admitted */
	  if (desc.cc.active)
	    continue;		/* CC-template inter-row contract */
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
		continue;	/* strictly after the last writeback */
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
	  /* Front-end access: retire-before-issue admits equality with
	     the last pending retirement.  */
	  if (access_lb >= max_dist)
	    continue;
	  xtt_effect_set e = rvtt_insn_effects (region.rows[r].insns[ix]);
	  return drain_refuse (dump, drain_conflict_name (e),
			       region.rows[r].insns[ix]);
	}
      base += h.words_per_row;
      if (desc.keep_separator && region.rows[r].separator)
	base += 1;		/* pure-RWC word re-emitted verbatim */
    }

  /* The enumerated follower words must cover the whole horizon.  */
  if ((int) base < max_dist)
    return drain_refuse (dump, "drain-horizon-spill", nullptr);
  return true;
}

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
    /* Unknown delays refuse formation before emission ever runs; keep
       the refusing direction locally anyway (drain-delay-unproven is the
       existing CORE_DELAY_UNKNOWN path).  */
    return drain_refuse (dump, "drain-delay-unproven", nullptr);
  if (max_dist == 0)
    return true;		/* Nothing to elide; trivially proven.  */
  if (end >= next_end || next_end > region.rows.length ())
    return drain_refuse (dump, "drain-follower-opaque", nullptr);

  /* (a) The inter-run stream: separators only, all launch-latched.  */
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
	  /* Launch-latched-only mutator (L1) no later pass absorbs (the
	     dst-autoincr AIC_RWC_STEP contract), so its issued words hold
	     their stream slots: slot credit.  A raw `.ttinsn' word is
	     exactly one word by the extraction contract
	     (rvtt_raw_ttinsn_word); a typed pattern's word count is its
	     machine-description length in 4-byte Tensix words.  */
	  if (asm_noperands (pat) >= 0)
	    sep_credit += 1;
	  else
	    sep_credit += get_attr_length (insn) / 4;
	  break;
	case xtt_rwc_effect_t::INC:
	  /* Launch-latched-neutral (L1), but dst-autoincr may absorb it
	     (AIC_INCRWC): presence in the final stream is unproven, so it
	     earns no slot credit.  */
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

  /* (b)+(c) The next run's accesses and horizon coverage.  */
  if (!drain_follower_rows_ok (region, schedule, desc, h, end, next_end,
			       sep_credit, dump))
    return false;

  if (dump)
    /* NB the harness fire witness 'run-boundary drain elided'
       (_REVIEWED_FIRE_WITNESSES) is SPLIT across the two source lines
       below -- a literal source grep for the full witness misses it;
       the runtime dump line matches (FH audit witness-check gotcha).  */
    fprintf (dump, "Macro-planner drain-schedule: run-boundary drain"
	     " elided (drain=%d separator-credit=%u)\n",
	     h.max_dist, sep_credit);
  return true;
}

/* ----------------------------------------------------------------------
   Loop-backedge drain elision (the drain-route remainder, lane CA).

   A loop-body region has one boundary the intra-region proof above can
   never reach: its final run ends at the loop latch, so today the
   derived drain executes once per trip -- where the architecture
   requires it once per loop EXIT.  The backedge follower stream is not
   invisible: it is the in-body tail after the final run, plus anything
   ahead of the region at the loop-body head, plus the region's OWN
   first run in the next iteration -- the identical row-succession the
   adopted schedule already sequences run-internally.  The verdict
   below proves that stream with the same decoded slot arithmetic
   (drain_decode_horizon / drain_follower_rows_ok) after classifying
   every interposed instruction:

     - never-absorbed launch-latched pure-RWC words (SET/FACE class,
       the AIC_RWC_STEP contract) earn slot credit, absorbable INC
       words none -- the intra-region separator discipline verbatim;
     - proven-neutral scalar instructions (no call, no asm, no memory
       store -- a scalar can only touch Tensix state by delivering a
       word through the instruction FIFO, which is a memory store)
       earn no credit: they can only DELAY follower issue, the safe
       direction under H2;
     - anything else refuses by name.

   The architectural exit contract is PRESERVED, not weakened: the
   caller of this verdict emits the full derived drain on the loop's
   exit path, so no event ever reaches an unproven follower stream.
   The first trip trivially satisfies the proof (no pending events at
   loop entry).  The replay/MOP passes that later re-deliver the body
   can only ADD issue slots ahead of the follower words (record and
   playback words), which moves follower accesses later -- the safe
   direction, same as any dynamic stall (H2).  */

static void
drain_note_mem_store (rtx x, const_rtx, void *data)
{
  if (MEM_P (x))
    *(bool *) data = true;
}

/* Classify one interposed follower-stream instruction at the loop
   backedge.  Returns true when INSN is proven drain-neutral,
   accumulating slot credit into *CREDIT; otherwise false with the
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
	  /* A real effect conflict names its class (E1-E4).  */
	  *why = drain_conflict_name (e);
	  return false;
	}
      switch (e.rwc.kind)
	{
	case xtt_rwc_effect_t::SET:
	case xtt_rwc_effect_t::FACE:
	  /* Never-absorbed launch-latched words hold their stream slots
	     (AIC_RWC_STEP contract): slot credit, sized as in the
	     intra-region walk.  */
	  if (asm_noperands (pat) >= 0)
	    *credit += 1;
	  else
	    *credit += get_attr_length (insn) / 4;
	  return true;
	case xtt_rwc_effect_t::INC:
	  /* Absorbable (AIC_INCRWC): neutral, no slot credit.  */
	  return true;
	case xtt_rwc_effect_t::NONE:
	  /* Audited effect-free Tensix instruction: neutral; its slot
	     survival is unproven, so no credit.  */
	  return true;
	default:
	  return false;
	}
    }
  /* Unaudited Tensix instruction or unproven raw word: refuse.  */
  if (asm_noperands (pat) >= 0)
    return false;
  if (recog_memoized (insn) >= 0 && get_attr_type (insn) == TYPE_TENSIX)
    return false;
  /* A scalar RISC instruction.  It can only touch Tensix state by
     delivering a word through a memory store; refuse stores, admit
     pure register/branch scalars with no slot credit.  */
  bool stores_mem = false;
  note_stores (insn, drain_note_mem_store, &stores_mem);
  if (stores_mem)
    return false;
  return true;
}

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

  /* Tail walk: from the final run's end to the end of the loop body
     (the latch branch included).  */
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
      /* A discovery-admitted pure-RWC run separator surviving in the
	 tail is exactly the creditable launch-latched class; classify
	 it like any other stream insn (it earns its slot credit).  */
      if (!drain_stream_insn_neutral (insn, &credit, &why))
	return drain_refuse (dump, why, insn);
    }

  /* Head walk: anything ahead of the region at the loop-body head
     (executed after the backedge, before the region re-enters).  */
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


/* ----------------------------------------------------------------------
   Lane FT window-pairing: inter-row drain tuning (macro-planner emission
   service, under -mtt-tensix-optimize-window-pairing; the planner's
   emission consumes the verdict).

   The lane-EV inter-row obligation places the FULL derived drain between
   consecutive rows whenever any launch is a fixed-VD VALUE carrier -- a
   shape rule, deliberately register-blind.  This service derives the
   MINIMAL inter-row drain from the same architectural model the boundary
   and backedge proofs above already use (L1-L3/E1-E4/H1-H2, the
   retire-before-issue transactional model), made register-, Dst-, and
   sub-unit-exact:

     - The pending horizon is decoded from the descriptor's OWN
       SequenceBits (drain_decode_horizon's doctrine: the emitted words
       are the one derivation that can never drift from what the
       hardware sequences), cross-checked require-exact against the
       descriptor's emitted drain figure.  Each decoded event is mapped
       back to its origin row instruction through the schedule's
       (macro, realized-sub-unit) key; any unaccounted or ambiguous
       event refuses.
     - Each staged event carries its realized architectural footprint:
       the origin instruction's typed effect set, widened by the
       SFPLOADMACRO override rules decoded from its OWN SequenceBits
       byte (the launch VD joins the reads; the result register is the
       launch VD or LReg[16] per the VD16 bit; the store's value
       register follows the store override cases), plus the descriptor
       template's hidden LREG writes.  [ISA] BlackholeA0 SFPLOADMACRO.md
       functional model (Insn.VB/VC/VD overrides; store VD cases
       0x40/0x80).
     - A follower access ordered after every pending writeback needs no
       footprint at all (the established rule: front-end accesses admit
       equality under retire-before-issue; staged events keep the strict
       inequality).  An access INSIDE the horizon is admitted exactly
       when its footprint is disjoint from every not-yet-retired pending
       event: LREG read/write intersection, CC (one side writing), any
       configuration write, and Dst overlap all refuse; two staged
       events at one cycle additionally refuse on sub-unit equality (the
       occupancy rule core_check_subunit_occupancy already enforces
       within a row; SFPLOADMACRO.md's four concurrent columns are the
       architectural basis for admitting distinct sub-units).
     - Dst overlap uses the audited physical-row model (the
       gimple-rvtt-transp-involution.cc access-rows audit; the
       rtl-rvtt-lp-alloc.cc dst32b window is the same fact): an access at
       constant address A touches lane rows [A & ~3, (A & ~3) + 3],
       mapped through dst32b_adjust_row for the 32-bit format class, with
       the configuration-resolved classes taking the union.  Disjoint row
       sets prove disjointness outright.  Overlapping row sets are still
       disjoint when the two accesses select OPPOSITE column parities --
       Column = (Lane & 7)*2 + ((Addr & 2) || DEST_{RD,WR}_COL_EXCHANGE)
       in both functional models (SFPLOAD.md, SFPSTORE.md), and the
       column index is preserved into the underlying DstBits storage by
       both the 16-bit and the 32-bit view (Dst.md) -- PROVIDED the
       column-exchange LaneConfig bits are architecturally default.  The
       parity clause therefore demands the same LaneConfig discipline the
       DSATUR spill machinery ships (rtl-rvtt-lp-alloc.cc,
       lreg-spill-laneconfig-unproven): any function-local write that
       could reach SFPCONFIG destination 15 -- a typed dest-15
       configuration write, a call, an unproven asm word, an unaudited
       Tensix instruction -- refuses the clause; the ambient default (no
       column exchange; the simulator's reset state, what the LLK init
       sequence leaves in place per the audited dest-15 table) is the
       flag's documented platform contract, identical to the spill
       flag's.  Mod0 10 (INT32_ALL) refuses: it couples the address to
       the Sp counter and mutates it (SFPLOAD.md), breaking the
       shared-RWC-base distance model.
     - Row-to-row Dst distance is the schedule's absorbed typed stride.
       The advancing address mode provably rides the row's LAST issued
       word and every other access carries the architectural
       no-increment mode (checked from the launch specs and the explicit
       operands), and SFPLOAD/SFPSTORE apply their address-modifier
       AFTER resolving their own address (SFPLOAD.md functional-model
       order), so every access of row r resolves at the same counter
       value and row r+j sits exactly j strides away.  Pending stores
       latch their Dst row at launch (L1), so follower counter advances
       never move them.

   The verdict is the smallest inter-row NOP count n (0 <= n <
   drain_slots) whose follower stream -- the next rows at spacing n --
   passes every rule, or drain_slots (today's bytes) with the binding
   blocker named.  Every distance is a stream-slot count: H2 makes it a
   lower bound on issue-cycle distance, and dynamic stalls only move
   followers later (the safe direction).  Emission under refusal is
   byte-identical to lane EV's placement.  Frozen whole-word programs
   whose events the schedule cannot account for refuse through the
   mapping rule -- the signbit family stays on its proven rolled
   calendar.  */

namespace {

/* One staged (launched) event with its realized footprint.  */
struct wp_event
{
  int exec;			/* in-row execution time: carrier word
				   position + 1 + decoded delay (the
				   drain_decode_horizon convention)    */
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
};

/* Audited Dst physical-row model (see the comment above; the same model
   gimple-rvtt-transp-involution.cc:access_rows audits).  */

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

/* Whether the column-exchange LaneConfig bits are provably at their
   architectural default throughout FN: no reachable writer of SFPCONFIG
   destination 15 -- typed dest-15 writes, calls, unproven asm words, and
   unaudited Tensix instructions all refuse.  Scalar (non-Tensix)
   instructions cannot issue an SFPCONFIG.  Ambient state is the flag's
   documented platform contract (rtl-rvtt-lp-alloc.cc discipline).  */

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

/* Dst disjointness of two typed accesses under the audited model.
   LANECFG is the lazily computed tri-state (-1 unknown / 0 unproven /
   1 proven).  *WHY names the failing clause.  */

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

/* Data-conflict verdict between follower access F and pending event P
   (order not architecturally established).  Returns the stable blocker
   name, or null when provably independent.  */

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

/* Typed constant Dst operands of ORIGIN (post-admission), shifted by
   SHIFT strides.  Fails soft: leaves *EV untyped (any Dst pairing then
   refuses by name).  Mod0 10 (INT32_ALL) is never typed here (Sp-coupled
   addressing; see the header comment).  */

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

/* Complete EV's realized footprint from its origin instruction, its
   launch spec, and its already-decoded SequenceBits fields, with its
   Dst address shifted by SHIFT strides.  Returns false with *WHY set on
   any unproven piece.  */

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

} /* anonymous namespace */

/* Derive the minimal proven inter-row drain for REGION's uniform rows.
   Returns a value in [0, desc.drain_slots]; desc.drain_slots (today's
   bytes) on any refusal, with the blocker named to DUMP.  When the
   verdict is positive but below the full drain, *BOUND_NAME names the
   conflict that bounds it from below (the blocker at one NOP fewer).  */

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

  /* Rows are isomorphic to row 0 only UNDER A VALUE MAP -- their emitted
     registers and typed operands may differ per row while the derived
     footprints below come from row 0.  The tune is proven only when
     every row's per-position effect sets and typed Dst operands are
     IDENTICAL to row 0's (the pinned-VD calendars this service targets
     are exactly the identical-register shapes); any variance refuses by
     name and keeps the full drain.  */
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

  /* Issue-word positions in emission order (slots ascending), the same
     mapping the boundary proofs use.  */
  auto_vec<int> word_pos;
  word_pos.safe_grow_cleared (schedule.events.length ());
  for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
    word_pos[ix] = -1;
  int carrier_pos[8];
  for (int m = 0; m != 8; ++m)
    carrier_pos[m] = -1;
  int words_per_row = 0, last_issue = -1;
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
	last_issue = ev.slot;
      }
  if (words_per_row == 0)
    return refuse ("window-pairing-footprint-opaque");
  for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
    if (schedule.events[ix].issues_word && word_pos[ix] < 0)
      return refuse ("window-pairing-footprint-opaque");
  int last_pos = words_per_row - 1;

  /* Every Dst access of the row must resolve at one counter value with
     the single absorbing advance riding the row's LAST issued word:
     then consecutive rows sit exactly one stride apart (SFPLOAD.md
     functional-model order: the address resolves before the modifier
     applies).  The launches' EMITTED address modes live in the launch
     specs (the origin operands predate absorption); explicit accesses
     are emitted with their own typed operands.  */
  int stride = schedule.absorbed_stride;
  /* Issue-word position of the single absorbing advance.  The compact
     form (absorber on the row's LAST issued word) is the established
     proof; under -mtt-tensix-optimize-window-pairing-stride an absorber
     riding an EARLIER word is admitted and every Dst footprint below is
     rebased by its carrying word's stride phase (0 at or before the
     absorber, 1 after it): the absorber's own access resolves before
     ApplyPartialAddrMod runs (F5) and SFPLOADMACRO-hosted events latch
     their Dst row at the launch word regardless of later advances (L1;
     SFPLOADMACRO.md StoreSubUnit Addr-resolution extra), so a word's
     POSITION relative to the absorber decides which counter value its
     accesses resolved at -- see rvtt-cost.md F5'.  */
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

  /* Stride phase of schedule event IX: 0 when its carrying word sits at
     or before the absorbing word (its Dst address resolved at the
     row-entry counter value), 1 when after it (resolved one stride
     later).  The carrying word is the event's own issued word, or its
     carrier's launch word for launched template slots (Dst row latched
     at launch: L1).  -1 = no provable carrying word (fail closed).
     With the absorber on the last issued word every phase is 0 and the
     arithmetic below is the established compact-form model verbatim.  */
  auto stride_phase = [&] (unsigned ix) -> int
    {
      const macro_event &ev = schedule.events[ix];
      int carry = ev.issues_word ? word_pos[ix]
	: ev.macro_index < 8 ? carrier_pos[ev.macro_index] : -1;
      if (carry < 0)
	return -1;
      return carry > absorber_pos ? 1 : 0;
    };

  /* The staged events, decoded from the descriptor's OWN SequenceBits
     (the derivation that can never drift from what the hardware
     sequences) and mapped back to their origin row instructions through
     the schedule's (macro, realized-sub-unit) key.  Every launched
     schedule event must be accounted for exactly once; the decoded
     horizon must equal the descriptor's emitted drain figure.  */
  auto_vec<wp_event> staged;
  unsigned launched_total = 0;
  for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
    if (schedule.events[ix].realization
	== macro_event::LAUNCHED_TEMPLATE_SLOT)
      ++launched_total;
  int max_pend = 0;
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
	  if (found < 0)
	    return refuse ("window-pairing-footprint-opaque");
	  wp_event ev;
	  memset (&ev, 0, sizeof (ev));
	  ev.sched_ix = (unsigned) found;
	  ev.macro_index = m;
	  ev.subunit = su;
	  ev.kind = kind;
	  ev.vd16 = vd16;
	  ev.route_vb = route_vb;
	  ev.exec = carrier_pos[m] + 1 + (int) delay;
	  if (ev.exec - last_pos > max_pend)
	    max_pend = ev.exec - last_pos;
	  staged.safe_push (ev);
	}
    }
  if (staged.length () != launched_total)
    return refuse ("window-pairing-footprint-opaque");
  if (max_pend != full)
    return refuse ("window-pairing-delay-unproven");

  /* Pending horizon: the staged events still in flight past the row's
     last issued word, with their realized footprints (unshifted -- row
     r's own addresses).  */
  auto_vec<wp_event> pending;
  for (const wp_event &sv : staged)
    if (sv.exec > last_pos)
      {
	wp_event p = sv;
	p.exec = sv.exec - last_pos;	/* boundary-relative */
	int phase = stride_phase (sv.sched_ix);
	if (phase < 0)
	  return refuse ("window-pairing-stride-unproven");
	const char *why = nullptr;
	if (!wp_event_footprint (desc, c, region.rows[0].insns[p.sched_ix],
				 stride * phase, &p, &why))
	  return refuse (why);
	pending.safe_push (p);
      }

  /* Ascending search for the smallest admissible spacing.  */
  int lanecfg = -1;
  const char *blocker_at[8] = {};
  int best = full;
  for (int n = 0; n < full; ++n)
    {
      const char *blocker = nullptr;
      for (unsigned j = 1; !blocker; ++j)
	{
	  /* Follower row j at credit n: its first word issues at
	     boundary-relative slot base+1.  Beyond the horizon every
	     later access clears by time (H1+H2); run-tail rows only add
	     the full run-end drain after them (more slack).  */
	  int base = n + (int) (j - 1) * (words_per_row + n);
	  if (base + 1 > full)
	    break;
	  /* Front-end issued words: ordered after every pending
	     writeback at or past their issue slot (retire-before-issue
	     admits equality).  */
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
		  /* The launch's own front-end SFPLOAD: writes the
		     launch VD, reads its carried Dst address (the
		     store-only carrier's sacrificial load included).  */
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
		     planned register (the template src field / the
		     coalesced launch VD); the footprint takes the
		     union.  */
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
	  /* Launched events of the follower row: strict ordering against
	     every pending event; same-cycle admits only distinct
	     sub-units with disjoint data (the occupancy rule).  */
	  for (unsigned sx = 0; sx != staged.length () && !blocker; ++sx)
	    {
	      int exec_f = base + 1 + staged[sx].exec;
	      if (exec_f > full)
		continue;	/* strictly after every pending writeback */
	      wp_event f = staged[sx];
	      f.exec = exec_f;
	      int fphase = stride_phase (f.sched_ix);
	      if (fphase < 0)
		{
		  blocker = "window-pairing-stride-unproven";
		  break;
		}
	      const char *why = nullptr;
	      if (!wp_event_footprint (desc, c,
				       region.rows[0].insns[f.sched_ix],
				       stride * ((int) j + fphase), &f, &why))
		{
		  blocker = why;
		  break;
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
