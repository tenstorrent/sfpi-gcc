/* Tensix scheduling: latency-bubble and NOP-shadow filling
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

/* The latency-scheduling unit of the Tensix scheduler
   (-mtt-tensix-optimize-latency-schedule): fill_latency_bubbles
   moves one instruction behind an exposed one-slot result-latency
   bubble, and fill_nop_shadows finds an independent filler for
   exactly the bubbles the NOP inserter would pad.  Also defines
   the shared register-set collectors and the delay-contract probe
   (delay_nop_needed_p) the other scheduler units and the NOP
   inserter consume (rtl-rvtt-sched-int.h).  Split from
   rtl-rvtt-schedule.cc; the algorithm essay lives there.  */

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
#include "rtl-rvtt-sched-int.h"

/* Collect INSN's SFPU hard-register uses and defs into REGS from DF.
   Returns false when any reference is a pseudo or a non-SFPU register,
   or when INSN defines no SFPU register -- the callers' schedulable-
   node contract (pure vector operations with a vector result).  */

bool
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

/* Whether INSN may participate in the simple latency-bubble move: a
   recognized Tensix instruction whose reorderability is audited
   (xtt_latency_reorder "safe"), with no memory reference and with
   pure SFPU register references, collected into REGS.  */

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

/* The next issued instruction after INSN in BB: the first following
   non-debug insn, provided it is a recognized Tensix instruction with
   a nonzero length.  Returns null at the block end or when that first
   insn is anything else -- callers treat such a stop as an opaque
   boundary, not something to skip.  */

rtx_insn *
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

/* Whether A and B share a register: a short-named wrapper for
   hard_reg_set_intersect_p, keeping fill_latency_bubbles' six-way
   legality conjunction readable.  */

static bool
intersect_p (const HARD_REG_SET &a, const HARD_REG_SET &b)
{
  return hard_reg_set_intersect_p (a, b);
}

/* Move one independent ready instruction into a single exposed result-latency
   slot.  The existing delay pass runs afterward and remains the authority for
   target scoreboarding and WH/BH/QSR errata.  */
void
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

/* The generated target cost hook deliberately returns one for the existing
   STATIC/DYNAMIC contracts.  Do not generalize this to instruction distance:
   that needs a separate walk over emitted Tensix insns.  */
unsigned
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
      /* Each block, other than the starting block, should only be
         walked once -- don't get trapped in a loop of non-TENSIX
         insns. The starting block should be walked exactly twice, if
         reachable from itself.  */
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
	  /* The case where this would be a dependency does not arise.  */
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
	      /* Unknown tensix insn component */
	      gcc_unreachable ();

	    case PARALLEL:
	    case UNSPEC:
	    case UNSPEC_VOLATILE:
	      {
		/* All 3 have the vector at position 0 */
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
	    /* BH & QSR has scoreboarding, but with bugs */
	    if (!(mask & get_attr_xtt_dynamic_bug (probe_insn)))
	      is_dependent = false;

	if (!is_dependent && !get_attr_length (probe_insn))
	  continue;

	if (dump_file)
	  {
	    fprintf (dump_file, "Found %sdependent insn ",
		     is_dependent ? "" : "non-");
	    dump_insn_slim (dump_file, probe_insn);
	  }
	return is_dependent;
      }

  /* Walk all the successors */
  edge_iterator ei;
  edge e;
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (find_next_insn (visited, e->dest, regno, BB_HEAD (e->dest), true))
      return true;

  return false;
}

/* Decide whether the nop inserter below would pad INSN's delay: the exact
   probe transform uses, factored out so the shadow-filling phase can target
   (and re-verify) precisely the bubbles that would otherwise become SFPNOPs.
   DELAY must be INSN's non-NONE delay contract.  */

bool
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

bool
bare_lreg_copy_p (rtx_insn *insn)
{
  rtx set = single_set (insn);
  return set && REG_P (SET_DEST (set)) && SFPU_REG_P (REGNO (SET_DEST (set)))
    && REG_P (SET_SRC (set)) && SFPU_REG_P (REGNO (SET_SRC (set)));
}

/* Whether INSN is a recognized Tensix instruction that delivers at
   least one instruction word: excludes USE/CLOBBER markers and
   zero-length bookkeeping patterns.  */

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

/* SFPU-register references of INSN from DF; other references (scalar
   registers, memory) cannot conflict with a pure-LREG filler.  */

void
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

/* Whether a moving filler may cross INSN.  A replay-buffer owner is
   never crossable (a fixed capture records delivered words by
   position).  When HIDDEN_FREE_FILLER (the bare all-lanes copy, which
   is invariant to CC, Dst, RWC, and configuration state), any other
   recognized Tensix word may be crossed on register facts alone;
   otherwise INSN must be provably non-CC-writing: audited
   reorder-safe, a bare copy, or on record with no CC write.  */

bool
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

bool
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

/* The generalized shadow filler (see the section comment above).  For
   every dynamic-delay producer in FN whose bubble the nop inserter
   would pad, search up to SEARCH_WINDOW candidates further down the
   block for a provably independent filler and move it into the bubble.
   The move commits only when the probe confirms the producer's bubble
   closed, the filler opened none of its own, and no new pad site
   appeared at the vacated position; otherwise it is undone.  */

void
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
