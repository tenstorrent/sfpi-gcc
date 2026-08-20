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
#include "print-rtl.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "recog.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-macro-region.h"
#include "rvtt-macro-sched.h"
#include "rvtt-macro-desc.h"

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

static void
fill_nop_shadows (function *fn)
{
  std::vector<basic_block> visited;
  std::vector<rtx_insn *> crossed_insns;
  constexpr unsigned SEARCH_WINDOW = 24;

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
     the two-adjacency stall accounting below is exact;
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
  if (e.opaque)
    return -1;
  // Lane BM (minimal, coordinated with the drain-model work): an
  // instruction with the architectural next-slot ACCEPTANCE stall
  // (xtt_next_slot_stall; SFPSWAP.md) keeps refusing here even once it
  // carries an audited result latency for the reissue-pricing model --
  // this preserves the pass's documented pre-audit behavior exactly
  // ("SFPSWAP ... never becomes a fill target").
  if (e.next_slot_stall)
    return -1;
  return e.result_latency;
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
  if (!hard_reg_set_intersect_p (p_regs.defs, c_regs.uses)
      && !hard_reg_set_intersect_p (p_regs.defs, c_regs.defs))
    return 0;
  return audited_latency (p);
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
  constexpr unsigned SEARCH_WINDOW = 24;

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
	    if (dump_file)
	      fprintf (dump_file, "Capture rotation refused: row-step "
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
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: row-step "
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
	    fprintf (dump_file, "Capture rotation refused: %s in bb %d\n",
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
  OPTGROUP_NONE, /* optinfo_flags */
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
      fprintf (dump, "Macro-planner drain-refusal: %s", name);
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
   is counted at issue (the conservative earliest).  Ordering at equal
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
