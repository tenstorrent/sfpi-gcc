/* Tensix scheduling: interlock-shadow filling
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

/* The interlock-scheduling unit of the Tensix scheduler
   (-mtt-tensix-optimize-interlock-schedule): fills the transparent
   scoreboard stalls modeled by the audited result-latency facts.
   Also defines the audited-latency and adjacency-stall wrappers
   the region, pairing and rotation units consume
   (rtl-rvtt-sched-int.h).  Split from rtl-rvtt-schedule.cc; the
   algorithm essay lives there.  */

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

int
audited_latency (rtx_insn *insn)
{
  if (!issued_tensix_p (insn))
    return -1;
  xtt_effect_set e = rvtt_insn_effects (insn);
  /* Lane BM (minimal, coordinated with the drain-model work): an
     instruction with the architectural next-slot ACCEPTANCE stall
     (xtt_next_slot_stall; SFPSWAP.md) keeps refusing here even once it
     carries an audited result latency for the reissue-pricing model --
     this preserves the pass's documented pre-audit behavior exactly
     ("SFPSWAP ... never becomes a fill target").  That discipline,
     like every timing rule, has ONE spelling: the timing engine's
     (verdict identity proven by the stage-A shadow over a full corpus
     -fchecking leg, zero disagreements).  */
  return rvtt_timing::audited_latency (e.opaque, e.next_slot_stall,
				       e.result_latency);
}

/* Modeled interlock stall cycles between issued P and an immediately
   following issued C.  0 when independent (or either is missing); the
   audited latency when dependent; -1 (refuse) when dependent on an
   unaudited producer.  */

int
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

/* The interlock-stall filler (see the section comment above).  For
   every issued producer in FN with an audited one-slot latency and an
   immediately following dependent consumer -- a modeled transparent
   scoreboard stall, not a required-nop site -- search the bounded
   window for an independent zero-latency filler and move it into the
   stall.  The move commits only when the modeled stall count strictly
   decreases over every changed adjacency (any unaudited term refuses)
   and the required-nop guards stay clean; otherwise it is undone
   byte-identically.  Targets without audited latency facts refuse
   wholesale.  */

void
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
