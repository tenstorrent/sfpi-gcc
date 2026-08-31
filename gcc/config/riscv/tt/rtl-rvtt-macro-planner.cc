/* Generic SFPLOADMACRO macro planner (analysis skeleton).
   Copyright (C) 2026 Tenstorrent Inc.

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

#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "rtl.h"
#include "tree-pass.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "recog.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "basic-block.h"
#include "cfgrtl.h"
#include "cfghooks.h"
#include "df.h"
#include "rtl-iter.h"
#include "tm_p.h"
#include "rvtt-protos.h"
#include "rvtt-refuse.h"
#include "rvtt-effects.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-macro-region.h"
#include "rvtt-macro-sched.h"
#include "rvtt-macro-desc.h"
#include "rvtt-macro-epoch.h"
#include "rvtt-raw-boundary.h"

/* The macro planner replaces every exact-calendar SFPLOADMACRO
   recognizer with regions, schedules, and descriptors derived from typed
   effects, dataflow proofs, and capability tables.  This pass is the
   planner's spine; at this stage it is analysis-only: under
   -mtt-tensix-macro-planner-analyze it reports discovered regions and
   named refusals to its dump and never mutates the function.  It runs
   after IRA/reload (hard LREGs final) and before the quarantined
   exact-calendar pass, the hazard scheduler, and replay formation.  */

namespace {

/* ---------------- Formation (WP7): emission from the descriptor ------ */

/* Function-global configuration-ownership proof, typed: the planner owns
   the macro configuration destinations and address-modifier slots for
   the whole function under the formation contract, so any call, any
   asm, and any typed config access touching an owned destination (or a
   statically unknown one) refuses.  Path-sensitive refinement through
   rvtt-macro-ownership is a documented later widening (fresh tests
   required); this matches the frozen contract byte for byte.  */

static bool
planner_config_ownership_ok (function *fn, const rvtt_macro::caps *c)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  if (CALL_P (insn))
	    return false;
	  xtt_effect_set e = rvtt_insn_effects (insn);
	  if (asm_noperands (PATTERN (insn)) >= 0 && e.opaque)
	    /* Raw asm refuses unless the audited `.ttinsn' decode
	       proves it a pure Dst/RWC counter word (rvtt-raw-boundary
	       via rvtt_insn_effects) -- such a word touches no
	       configuration destination by that proof.  */
	    return false;
	  if (!e.opaque
	      && ((e.config_dests_written | e.config_dests_read)
		  & c->owned_config_dests))
	    return false;
	}
    }
  return true;
}

/* Region-scoped configuration ownership for a loop-body region: the
   fallback when the function-global proof fails because the enclosing
   function carries foreign Tensix code (the real-kernel shape of the
   typecast blocker -- the four architectural faces sit in a loop inside
   a function full of opaque init/dataflow instructions, so the
   function-global scan can never prove ownership there).

   The configuration window is the proven structural preheader's TAIL
   (the compiler-owned insertion point after the last reachable foreign
   call, asm, or configuration access) plus the loop body.  Placement at
   the tail dominates every trip's launches, and the window proof shows
   no path from the materialization point to the final drain contains
   another owner: the preheader's unique successor is the body (proven
   by loop_region_preheader), the body's only edges are its self-loop
   and its exit, and this scan proves every body instruction is either a
   region-owned issue or provably inert scalar code -- no call, no asm,
   no Tensix issue, and no volatile memory reference (the shape of every
   raw MMIO instruction push or configuration access the typed effect
   vocabulary cannot see).  Foreign owners BEFORE the insertion point
   are simply overwritten by the prefix.  Code after the loop exit runs
   after the drain and is beyond the descriptor's lifetime, exactly as
   when the planner forms inside an out-of-line callee invoked from an
   opaque caller (the shipped straight-line contract).  */

static bool
planner_config_window_ok (const macro_region &region)
{
  basic_block body = region.bb;
  for (rtx_insn *insn = BB_HEAD (body); insn; insn = NEXT_INSN (insn))
    {
      if (NONDEBUG_INSN_P (insn))
	{
	  bool owned = false;
	  for (const macro_row &row : region.rows)
	    {
	      owned |= insn == row.enable || insn == row.separator;
	      for (rtx_insn *member : row.insns)
		owned |= insn == member;
	    }
	  for (rtx_insn *sep : region.run_separators)
	    owned |= insn == sep;
	  if (!owned)
	    {
	      if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0)
		return false;
	      if (recog_memoized (insn) >= 0
		  && get_attr_type (insn) == TYPE_TENSIX)
		return false;
	      if (volatile_refs_p (PATTERN (insn)))
		return false;
	    }
	}
      if (insn == BB_END (body))
	break;
    }
  return true;
}

/* Region-scoped configuration ownership (WP9; the refinement the
   function-global proof documents).  Used ONLY for proven CC-template
   programs, and only as the ADDITIONAL fallback after the loop-scoped
   window proof above (which covers any loop-body region) has been
   tried: this path adds the straight-line CC-template shapes the
   window path does not reach.  Every other shape keeps the
   conservative function-global gate above, so their formation and
   refusal behavior is unchanged.

   Soundness bounds, matching the deleted quarantined pass's select
   contract ("no other Tensix issue may own config, CC, LREG, or
   calendar state between the materialization and a launch"):

   - The planner's configuration prefix rewrites EVERY owned destination
     the calendar consumes (templates, sequence words, misc; the CC
     programs absorb no stride, so no address-modifier slot is read), so
     foreign configuration writes BEFORE the prefix are dead.

   - Between the prefix placement point and the region end -- the
     preheader tail plus the loop body for a loop-body region, or the
     region span itself for a straight-line one -- there must be no
     call, no inline assembly, and no typed access to an owned
     configuration destination.  For a loop-body region the scope also
     covers the chain from the consumed trailing enable, so no opaque
     issue can sit between the lane-state proof and the loop.

   - Foreign code AFTER the region is tolerated: the LLK ownership
     convention (carried from the frozen pass and its silicon-proven
     integrations) is that every SFPLOADMACRO consumer programs its own
     descriptors before launching.  This is a documented accepted risk,
     mirrored in docs/MACRO_PLANNER.md.  */

static bool
planner_scope_insn_clean_p (rtx_insn *insn, const rvtt_macro::caps *c)
{
  if (!NONDEBUG_INSN_P (insn))
    return true;
  if (CALL_P (insn))
    return false;
  xtt_effect_set e = rvtt_insn_effects (insn);
  if (asm_noperands (PATTERN (insn)) >= 0 && e.opaque)
    /* Raw asm refuses unless the audited `.ttinsn' decode proves it a
       pure Dst/RWC counter word (rvtt-raw-boundary via
       rvtt_insn_effects).  */
    return false;
  if (!e.opaque
      && ((e.config_dests_written | e.config_dests_read)
	  & c->owned_config_dests))
    return false;
  return true;
}

static bool
planner_region_config_ownership_ok (const macro_region &region,
				    basic_block config_preheader,
				    rtx_insn *scope_begin,
				    const rvtt_macro::caps *c)
{
  /* The region's own basic block from the scope begin (the prefix
     anchor for straight-line regions; the block head for loop bodies,
     whose every trip re-executes under the preheader-materialized
     configuration).  */
  basic_block bb = region.bb;
  rtx_insn *from = config_preheader ? BB_HEAD (bb) : scope_begin;
  for (rtx_insn *insn = from; insn; insn = NEXT_INSN (insn))
    {
      if (!planner_scope_insn_clean_p (insn, c))
	return false;
      if (insn == BB_END (bb) || insn == region.last)
	break;
      if (!config_preheader && insn == region.last)
	break;
    }
  /* For a loop-body region: the preheader from the prefix insertion
     point to the loop entry.  The prefix is placed at the block end
     (before a trailing jump), so only the jump can follow it --
     verified here rather than assumed.  */
  if (config_preheader)
    {
      rtx_insn *tail = BB_END (config_preheader);
      if (tail && !JUMP_P (tail) && !planner_scope_insn_clean_p (tail, c))
	return false;
    }
  return true;
}

/* Prove that hard register VALUE has no use after START before an
   all-lane definition kills it (the frozen pass's proof idiom).  */

static bool
planned_value_dead_after_p (rtx value, rtx_insn *start)
{
  basic_block bb = BLOCK_FOR_INSN (start);
  for (rtx_insn *insn = NEXT_INSN (start);
       insn && BLOCK_FOR_INSN (insn) == bb; insn = NEXT_INSN (insn))
    if (NONDEBUG_INSN_P (insn))
      {
	if (reg_referenced_p (value, PATTERN (insn)))
	  return false;
	if (reg_set_p (value, insn))
	  return true;
      }
  return !bitmap_bit_p (df_get_live_out (bb), REGNO (value));
}

/* Issue cost of materializing one 32-bit configuration word through an
   LREG: the SFPLOADI half count mirrors rvtt_emit_sfpxloadi's forms --
   the one delivery-cost spelling (rvtt-delivery-cost-core.h
   loadi_issue_words; FABLE_GOES_BURR #12).  */

static unsigned
config_word_loadi_issues (uint32_t w)
{
  unsigned issues = rvtt_dcost_loadi_issue_words (w);
  if (flag_checking)
    {
      /* One-pin recompute-assert of the migrated inline spelling
	 (item #12 discipline; delete next pin).  */
      unsigned old;
      if (w <= 0x7fff || w >= 0xffff8000u || w <= 0xffff || !(w & 0xffff))
	old = 1;
      else if (!(w & 0x1fff)
	       && ((w >> 23) & 0xff) < 127 + 16
	       && ((w >> 23) & 0xff) >= 127 - 14)
	old = 1;
      else
	old = 2;
      gcc_assert (old == issues);
    }
  return issues;
}

/* Issue cost of the full configuration prefix (all-lanes enable, owned
   SETC16 program, and every descriptor word's materialization).  */

static unsigned
config_prefix_cost (const macro_descriptor &desc)
{
  unsigned config_cost = 1;	/* all-lanes enable */
  config_cost += desc.n_setc16;
  for (unsigned t = 0; t != desc.n_templates; ++t)
    config_cost += config_word_loadi_issues (desc.templ[t]) + 1;
  for (unsigned m = 0; m != desc.n_seq; ++m)
    config_cost += config_word_loadi_issues (desc.seq[m]) + 1;
  if (desc.has_misc)
    config_cost += config_word_loadi_issues (desc.misc) + 1;
  return config_cost;
}

/* Issue cost of one explicit (unformed) row.  */

static unsigned
explicit_row_cost (const macro_region &region)
{
  const macro_row &row = region.rows[0];
  return row.insns.length ()
    + (row.enable ? 1 : 0) + (row.separator ? 1 : 0);
}

/* Layer-6 profitability, derived from configuration and drain costs --
   no row thresholds anywhere.  Every run must independently amortize
   the full configuration prefix (the frozen conservative-per-run
   discipline): rows*ii + drain + config < rows * explicit-row issues.

   Init-hoist-aware run pricing (lane IU, 2026-08-29; rvtt-cost.md
   caller-loop prefix amortization): when the crosscall init hoist's
   FULL contract (stage 2) is already proven for this region -- the
   proof-only pre-run ahead of this gate, committed after every later
   refusal point -- the configuration prefix will not be emitted in
   the callee at all: the init contract words execute once per proven
   caller-loop entry, not once per run.  Pricing then weighs the
   prefix by the caller loop's profile entry/body fraction, exactly
   the loop_profitable_p discipline one call level up:
     config * entry + (rows*ii + drain) * body < rows * explicit * body.
   Refusal-biased: every run still charges the FULL amortized prefix
   (n runs charge it n times); stage 1 (per-call enable+SETC16
   retained) and an unusable profile weight keep the frozen per-run
   pricing unchanged.  IH_STAGE is 0 unless the stage-2 proof and the
   weight both hold.  */

static bool
run_profitable_p (const macro_region &region, const macro_schedule &schedule,
		  const macro_descriptor &desc, unsigned run_rows,
		  int ih_stage, int64_t ih_entry, int64_t ih_body,
		  FILE *dump)
{
  uint64_t config = config_prefix_cost (desc);
  uint64_t per_run = (uint64_t) run_rows * schedule.ii + desc.drain_slots;
  uint64_t explicit_side = (uint64_t) run_rows * explicit_row_cost (region);
  if (ih_stage >= 2 && ih_body > 0 && ih_entry > 0 && ih_body >= ih_entry)
    {
      bool ok = rvtt_delivery_cost::run_amortized_p (config, per_run,
						     explicit_side,
						     (uint64_t) ih_entry,
						     (uint64_t) ih_body);
      /* One-pin recompute-assert of the migrated inline spelling
	 (item #12 discipline; delete next pin).  */
      if (flag_checking)
	gcc_assert (ok == (config * (uint64_t) ih_entry
			   + per_run * (uint64_t) ih_body
			   < explicit_side * (uint64_t) ih_body));
      if (dump)
	fprintf (dump, "Macro-planner run-pricing: init-hoist-amortized"
		 " config=%llu per-run=%llu explicit=%llu"
		 " weight=%lld/%lld -> %s\n",
		 (unsigned long long) config, (unsigned long long) per_run,
		 (unsigned long long) explicit_side, (long long) ih_body,
		 (long long) ih_entry, ok ? "profitable" : "unprofitable");
      return ok;
    }
  bool ok = rvtt_delivery_cost::run_amortized_p (config, per_run,
						 explicit_side, 1, 1);
  if (flag_checking)
    gcc_assert (ok == (config + per_run < explicit_side));
  return ok;
}

/* WP13 formation-vs-replay arbitration (-mtt-tensix-macro-ims).  The
   established profitability gates above price the formed calendar
   against RISC-pushed explicit rows word-for-word.  When the replay
   optimization is enabled and every row word is replay-admissible, the
   honest alternative is cheaper than that: the replay unit records the
   row once and re-executes it per instance with RISC delivery hidden
   under execution (the corrected concurrent-delivery accounting,
   rvtt-cost.md).  Formation must then ALSO price below that
   alternative, under the one shared issue-cost model
   (XTT_REPLAY_COST_*): the alternative is priced at its steady-state
   LOWER BOUND -- record-pass and launch delivery charged at zero --
   so the arbitration is refusal-biased: a formation that cannot beat
   even the ideal replay delivery of the same rows refuses by name
   (replay-delivery-preferred), keeping measured replay wins intact.
   Both sides are model outputs of the same constants; no operation
   identity participates.  Off, formation decisions are untouched.  */

static bool
region_rows_replay_safe_p (const macro_region &region)
{
  auto safe = [] (rtx_insn *insn) -> bool
    {
      return insn && recog_memoized (insn) >= 0
	&& get_attr_xtt_replay (insn) == XTT_REPLAY_SAFE;
    };
  const macro_row &row = region.rows[0];
  for (rtx_insn *insn : row.insns)
    if (!safe (insn))
      return false;
  if (row.enable && !safe (row.enable))
    return false;
  /* The typed Dst-counter separator is a replay barrier by itself, but
     the Dst auto-increment pass -- which runs after replay formation
     and absorbs exactly these separators around replay launches
     (rvtt-cost.md, the launch_run context term's own discount rule) --
     removes it from the replayed steady state when enabled.  Without
     that pass the separator survives, replay runs cannot form across
     rows, and the alternative stays the RISC-pushed stream the
     established gates already price.  */
  if (row.separator && !riscv_tt_opt_dst_autoincr)
    return false;
  return true;
}

/* Words one row instance re-executes in the replay-delivered steady
   state: the row's issue words minus the separator the auto-increment
   pass absorbs.  */

static unsigned
ims_replayed_row_words (const macro_region &region)
{
  unsigned w = explicit_row_cost (region);
  if (region.rows[0].separator && riscv_tt_opt_dst_autoincr && w > 0)
    --w;
  return w;
}

/* Centislot price of the formed calendar for RUN_ROWS rows: descriptor
   prefix delivered at the RISC push rate, launches and explicit words
   at the push rate (or the replay slot rate when the planner-replay
   delivery increment wraps them), drain at the slot rate.  */

static uint64_t
ims_formed_cost_x100 (const macro_schedule &schedule,
		      const macro_descriptor &desc, unsigned run_rows)
{
  uint64_t drain = desc.drain_slots > 0 ? desc.drain_slots : 0;
  uint64_t formed = rvtt_delivery_cost::ims_formed_cost_x100
    (rvtt_dcost_table (), config_prefix_cost (desc),
     (uint64_t) run_rows * (uint64_t) schedule.ii, drain,
     riscv_tt_macro_planner_replay != 0);
  /* One-pin recompute-assert of the migrated inline spelling
     (item #12 discipline; delete next pin).  */
  if (flag_checking)
    {
      uint64_t push = XTT_REPLAY_COST_RISC_PUSH_X100;
      uint64_t slot = XTT_REPLAY_COST_REPLAY_SLOT_X100;
      uint64_t word = riscv_tt_macro_planner_replay ? slot : push;
      gcc_assert (formed == (uint64_t) config_prefix_cost (desc) * push
		  + (uint64_t) run_rows * (uint64_t) schedule.ii * word
		  + drain * slot);
    }
  return formed;
}

/* Steady-state lower bound of the replay-delivered explicit
   alternative: every row instance re-executes its words at the slot
   rate with delivery hidden; record and launch words charged at
   zero (refusal-biased).  */

static uint64_t
ims_replay_alt_cost_x100 (const macro_region &region, unsigned run_rows)
{
  uint64_t alt = rvtt_delivery_cost::ims_replay_alt_cost_x100
    (rvtt_dcost_table (), run_rows, ims_replayed_row_words (region));
  if (flag_checking)
    gcc_assert (alt == (uint64_t) run_rows
			 * (uint64_t) ims_replayed_row_words (region)
			 * (uint64_t) XTT_REPLAY_COST_REPLAY_SLOT_X100);
  return alt;
}

static bool
ims_arbitrate_run (const macro_region &region, const macro_schedule &schedule,
		   const macro_descriptor &desc, unsigned run_rows,
		   int ih_stage, int64_t ih_entry, int64_t ih_body,
		   FILE *dump)
{
  if (!riscv_tt_macro_ims || !riscv_tt_opt_replay
      || !region_rows_replay_safe_p (region))
    return true;
  uint64_t formed = ims_formed_cost_x100 (schedule, desc, run_rows);
  uint64_t alt = ims_replay_alt_cost_x100 (region, run_rows);
  /* Init-hoist-aware arbitration (lane IU): under the proven stage-2
     contract the prefix's push words execute once per caller-loop
     entry; weigh both sides by the same profile fraction the run
     pricing uses (cross-multiplied, no rounding).  The replay
     alternative carries no prefix, so its side scales by body
     alone -- refusal-biased as before.  */
  if (ih_stage >= 2 && ih_body > 0 && ih_entry > 0 && ih_body >= ih_entry)
    {
      uint64_t prefix = (uint64_t) rvtt_dcost_words_to_centislots
	(config_prefix_cost (desc), rvtt_delivery_cost::PLANE_RISC_PUSH);
      if (flag_checking)
	gcc_assert (prefix == (uint64_t) config_prefix_cost (desc)
			      * XTT_REPLAY_COST_RISC_PUSH_X100);
      formed = prefix * (uint64_t) ih_entry
	+ (formed - prefix) * (uint64_t) ih_body;
      alt *= (uint64_t) ih_body;
    }
  if (dump)
    fprintf (dump, "Macro-planner ims-arbitration: formed=%llu"
	     " replay-alt=%llu (centislots; run-rows=%u ii=%d"
	     " row-words=%u) -> %s\n",
	     (unsigned long long) formed, (unsigned long long) alt,
	     run_rows, schedule.ii, ims_replayed_row_words (region),
	     formed < alt ? "form" : "replay-delivery-preferred");
  return formed < alt;
}

/* Loop-body analogue: the descriptor prefix is paid once per loop
   entry; per-trip launches and drains weigh against the per-trip
   replay-delivered alternative through the same profile ratio the
   established loop gate uses.  */

static bool
ims_arbitrate_loop (const macro_region &region,
		    const macro_schedule &schedule,
		    const macro_descriptor &desc, gcov_type body_count,
		    gcov_type preheader_count, unsigned n_runs, FILE *dump)
{
  if (!riscv_tt_macro_ims || !riscv_tt_opt_replay
      || !region_rows_replay_safe_p (region))
    return true;
  unsigned total_rows = region.rows.length ();
  uint64_t drain = desc.drain_slots > 0 ? desc.drain_slots : 0;
  uint64_t prefix = (uint64_t) rvtt_dcost_words_to_centislots
    (config_prefix_cost (desc), rvtt_delivery_cost::PLANE_RISC_PUSH);
  uint64_t per_trip = (uint64_t) rvtt_dcost_words_to_centislots
      ((uint64_t) total_rows * (uint64_t) schedule.ii,
       rvtt_delivery_cost::planner_word_plane
	 (riscv_tt_macro_planner_replay != 0))
    + (uint64_t) rvtt_dcost_words_to_centislots
	((uint64_t) n_runs * drain, rvtt_delivery_cost::PLANE_REPLAY_SLOT);
  uint64_t formed = prefix * (uint64_t) preheader_count
    + per_trip * (uint64_t) body_count;
  uint64_t alt = rvtt_delivery_cost::ims_replay_alt_cost_x100
      (rvtt_dcost_table (), total_rows, ims_replayed_row_words (region))
    * (uint64_t) body_count;
  /* One-pin recompute-assert of the migrated inline spelling
     (item #12 discipline; delete next pin).  */
  if (flag_checking)
    {
      uint64_t push = XTT_REPLAY_COST_RISC_PUSH_X100;
      uint64_t slot = XTT_REPLAY_COST_REPLAY_SLOT_X100;
      uint64_t word = riscv_tt_macro_planner_replay ? slot : push;
      gcc_assert (formed == (uint64_t) config_prefix_cost (desc) * push
			      * (uint64_t) preheader_count
			    + ((uint64_t) total_rows
			       * (uint64_t) schedule.ii * word
			       + (uint64_t) n_runs * drain * slot)
			      * (uint64_t) body_count);
      gcc_assert (alt == (uint64_t) total_rows
			   * (uint64_t) ims_replayed_row_words (region)
			   * slot * (uint64_t) body_count);
    }
  if (dump)
    fprintf (dump, "Macro-planner ims-arbitration: formed=%llu"
	     " replay-alt=%llu (centislots; loop rows=%u ii=%d"
	     " row-words=%u trip-weight=%lld/%lld) -> %s\n",
	     (unsigned long long) formed, (unsigned long long) alt,
	     total_rows, schedule.ii, ims_replayed_row_words (region),
	     (long long) body_count, (long long) preheader_count,
	     formed < alt ? "form" : "replay-delivery-preferred");
  return formed < alt;
}

/* Loop trip weight (WP8): the profile-estimated body/preheader
   execution-count ratio of a loop-body region.  Purely a profitability
   weight -- never a correctness input -- exact where the profile is
   (constant-bound loops), the static estimate elsewhere.  The two
   outputs report the ratio as an unreduced fraction so profitability
   can weigh it without rounding.  Returns false when the profile gives
   no usable estimate.  */

static bool
loop_trip_weight (basic_block body, basic_block preheader,
		  gcov_type *body_count, gcov_type *preheader_count)
{
  profile_count bc = body->count, pc = preheader->count;
  if (!bc.initialized_p () || !pc.initialized_p () || !pc.nonzero_p ())
    return false;
  gcov_type b = bc.to_gcov_type (), p = pc.to_gcov_type ();
  if (p <= 0 || b < p)
    return false;
  /* Keep the products of profitability inside 64 bits (the one
     scaling spelling, shared with the crosscall init-hoist caller
     weight: rvtt-delivery-cost-core.h scale_trip_weight).  */
  int64_t sb = b, sp = p;
  rvtt_delivery_cost::scale_trip_weight (&sb, &sp);
  /* One-pin recompute-assert of the migrated inline spelling
     (item #12 discipline; delete next pin).  */
  if (flag_checking)
    {
      gcov_type cb = b, cp = p;
      while (cb > (gcov_type) 1 << 48)
	{
	  cb >>= 8;
	  cp = cp >> 8 ? cp >> 8 : 1;
	}
      gcc_assert (sb == cb && sp == cp);
    }
  *body_count = sb;
  *preheader_count = sp;
  return true;
}

/* Loop-body profitability: the configuration prefix sits in the
   preheader and is paid once per loop entry, while every trip pays each
   run's launch calendar and drain against the explicit rows it
   replaces.  Weighted by the profile ratio without rounding:
   config * preheader_count + per_trip_macro * body_count
     < per_trip_explicit * body_count.  */

static bool
loop_profitable_p (const macro_region &region, const macro_schedule &schedule,
		   const macro_descriptor &desc, gcov_type body_count,
		   gcov_type preheader_count, unsigned n_runs)
{
  unsigned total_rows = region.rows.length ();
  unsigned per_trip_macro = total_rows * schedule.ii
    + n_runs * desc.drain_slots;
  unsigned per_trip_explicit = total_rows * explicit_row_cost (region);
  bool ok = rvtt_delivery_cost::run_amortized_p
    (config_prefix_cost (desc), per_trip_macro, per_trip_explicit,
     (uint64_t) preheader_count, (uint64_t) body_count);
  /* One-pin recompute-assert of the migrated inline spelling
     (item #12 discipline; delete next pin).  */
  if (flag_checking)
    {
      uint64_t macro_cost
	= (uint64_t) config_prefix_cost (desc) * preheader_count
	  + (uint64_t) per_trip_macro * body_count;
      gcc_assert (ok == (macro_cost
			 < (uint64_t) per_trip_explicit * body_count));
    }
  return ok;
}

/* An ambient lane-enable shape: only a CC write, no other
   architectural effect (the region scanner's pure-CC-write class).
   This classifies the SHAPE only; whether the written value provably
   enables all lanes is the separate cc_write_all_lanes proof, checked
   at every consumption site (an unproved value is a named
   cc-enable-unproved refusal, never an ambient enable).  */

static bool
pure_cc_write_insn_p (rtx_insn *insn)
{
  if (!NONDEBUG_INSN_P (insn))
    return false;
  xtt_effect_set e = rvtt_insn_effects (insn);
  return !e.opaque && e.cc_write && !e.cc_read
    && !e.lreg_read && !e.lreg_write
    && !e.config_dests_written && !e.addr_mod_slot_write
    && !e.dst_mem_read && !e.dst_mem_write
    && e.rwc.kind == xtt_rwc_effect_t::NONE;
}

/* The consumer-side all-lanes proof of an ambient enable: the written
   value must be word-exact against the capability table's
   architectural all-lanes SFPENCC encoding (rvtt_insn_effects derives
   the bit through the one shared derivation, so this proof can never
   drift from the encoding the hardware sees).  */

static bool
cc_enable_all_lanes_proved_p (rtx_insn *insn)
{
  return rvtt_insn_effects (insn).cc_write_all_lanes;
}

/* The trailing ambient enable of a proven loop preheader: the LAST
   Tensix issue in PREHEADER when it is a pure CC write.  With
   whole-body ownership (no CC writer inside the loop) this locates the
   instruction that decides the lane state at every trip's region
   entry, replacing the first row's local enable for regions whose
   enable was written once outside the loop.  This finds the SHAPE
   only; the caller must still prove the written value is the
   all-lanes pattern (cc_enable_all_lanes_proved_p) before consuming
   it.  */

static rtx_insn *
preheader_trailing_enable (basic_block preheader)
{
  /* Walk the unique-predecessor chain from the preheader upward until a
     Tensix issue is found: only scalar code may sit between the enable
     and the loop entry, so the dominating chain's LAST Tensix issue
     decides the proof.  */
  basic_block bb = preheader;
  for (unsigned depth = 0; depth != 16; ++depth)
    {
      rtx_insn *last_tensix = nullptr;
      for (rtx_insn *insn = BB_HEAD (bb); insn; insn = NEXT_INSN (insn))
	{
	  if (NONDEBUG_INSN_P (insn) && recog_memoized (insn) >= 0
	      && get_attr_type (insn) == TYPE_TENSIX)
	    last_tensix = insn;
	  if (insn == BB_END (bb))
	    break;
	}
      if (last_tensix)
	return pure_cc_write_insn_p (last_tensix) ? last_tensix : nullptr;
      if (!single_pred_p (bb)
	  || single_pred (bb) == ENTRY_BLOCK_PTR_FOR_FN (cfun))
	return nullptr;
      bb = single_pred (bb);
    }
  return nullptr;
}

/* Entry-ambient all-lanes derivation (lane IS, owner-ratified F1
   honest fix): whether the lane-enable state at the configuration
   placement point is provably the architectural all-lanes state, with
   NO marker instruction in the stream.  The ambient model is the
   established structured-CC lowering contract (gimple-rvtt-cc.cc: the
   outermost lane state is pinned all-lanes -- the same license behind
   the outermost POPC -> ENCC rewrite, the WP10 materialization, the
   crossrow-pairing loop-entry walk, and prgm-const's pre-peel walk):
   function entry is all-lanes ambient; a word-exact all-lanes SFPENCC
   KILLS (re-establishes the state); any other CC-affecting statement
   dirties.  Scalar (non-Tensix) instructions cannot touch SFPU lane
   state and are transparent.

   Audited-TU walk transparency (lane IV, typecast recovery): the v1
   walk was fail-closed on ALL asm -- the real LLK kernels inline their
   envelope init as raw `.ttinsn' TTI_ words, so every production
   preheader chain crossed "opaque" init and refused
   (ambient-entry-unproven).  The walk now DERIVES a verdict from the
   decoded content instead of refusing on shape (the lane HS
   record-window discipline: derive from decoded fields, never trust):

     - a canonical raw `.ttinsn' constant word classifies through the
       audited lane-enable table (rvtt_raw_cc_word_class,
       rvtt-raw-boundary.cc): a proven ambient-PRESERVING word (CC-inert
       class, or an ambient-establishing all-lanes write) is
       transparent; NEVER a kill -- a raw word can sit inside a REPLAY
       record load window where it is architecturally swallowed, so its
       execution cannot be asserted from the word alone
       (kill-classification would be unsound; preserving-classification
       is sound under both readings);
     - an empty asm template is a compiler barrier: no instruction, no
       lane-enable effect;
     - every remaining asm shape (MMIO store idioms, expander words
       such as MOP whose delivered content lives in template registers,
       unaudited words) leans on the TU-wide CC/lane-enable audit
       (rvtt_tu_opaque_cc_ambient_preserving_p): the gimple-time TU
       scan -- the same enumeration the PRGM freedom proof stands on --
       has classified every opaque-delivery channel in the TU (raw
       words, stores against the instruction-FIFO/aperture audit, MOP
       template slots, replay records, scalar asm) as unable to take
       the lane-enable state away from the all-lanes ambient.  A dirty
       or unavailable audit keeps the named refusal byte-identically.

   Calls, unrecognized instructions, and opaque typed Tensix effects
   stay DIRTY fail-closed (a call executes a whole body whose typed CC
   writers the TU audit deliberately does not cover).  */

static bool
entry_ambient_all_lanes_p (basic_block point_bb, rtx_insn *before,
			   FILE *dump)
{
  /* Refusal diagnostics: the first dirty instruction and its class.  */
  rtx_insn *dirty_insn = nullptr;
  const char *dirty_why = nullptr;
  unsigned derived_words = 0, tu_leaned = 0;

  /* 0 = transparent, 1 = kill (proven all-lanes), 2 = dirty.  */
  auto classify = [&] (rtx_insn *insn) -> int
    {
      auto dirty = [&] (const char *why) -> int
	{
	  if (!dirty_insn)
	    {
	      dirty_insn = insn;
	      dirty_why = why;
	    }
	  return 2;
	};
      if (!NONDEBUG_INSN_P (insn))
	return 0;
      if (CALL_P (insn))
	return dirty ("call");
      rtx pat = PATTERN (insn);
      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	return 0;
      if (asm_noperands (pat) >= 0)
	{
	  /* Lane IV: derive through the asm (header comment).  */
	  uint32_t word;
	  if (rvtt_raw_ttinsn_word (insn, &word))
	    {
	      if (rvtt_raw_cc_word_ambient_preserving_p (word))
		{
		  ++derived_words;
		  return 0;
		}
	      /* An unproven word could still ride the TU audit (an
		 expander word such as MOP is a TU-level question: its
		 delivered content is the audited template slots) --
		 fall through to the lean below.  */
	    }
	  else
	    {
	      /* Empty template = pure compiler barrier.  */
	      rtx aop = extract_asm_operands (pat);
	      const char *templ = aop ? ASM_OPERANDS_TEMPLATE (aop)
		: GET_CODE (pat) == ASM_INPUT ? XSTR (pat, 0) : nullptr;
	      if (templ)
		{
		  while (*templ == ' ' || *templ == '\t')
		    ++templ;
		  if (!*templ)
		    return 0;
		}
	    }
	  const char *reason = nullptr;
	  if (rvtt_tu_opaque_cc_ambient_preserving_p (&reason))
	    {
	      ++tu_leaned;
	      return 0;
	    }
	  return dirty (reason);
	}
      if (recog_memoized (insn) < 0)
	return dirty ("unrecognized");
      if (get_attr_type (insn) != TYPE_TENSIX)
	return 0;
      xtt_effect_set e = rvtt_insn_effects (insn);
      if (e.opaque)
	return dirty ("opaque-typed-effects");
      if (!e.cc_write)
	return 0;
      if (e.cc_write_all_lanes && pure_cc_write_insn_p (insn))
	return 1;
      return dirty ("cc-write");
    };

  /* Scan BB backwards, from just before STOP_BEFORE (or the block end
     when null), and report the last CC event: 1 kill, 2 dirty, 0 none
     (transparent -- the walk continues into the predecessors).  */
  auto scan = [&classify] (basic_block bb, rtx_insn *stop_before) -> int
    {
      rtx_insn *insn = stop_before ? PREV_INSN (stop_before) : BB_END (bb);
      while (insn && BLOCK_FOR_INSN (insn) == bb)
	{
	  int c = classify (insn);
	  if (c)
	    return c;
	  if (insn == BB_HEAD (bb))
	    break;
	  insn = PREV_INSN (insn);
	}
      return 0;
    };

  auto report = [&] (bool proven) -> bool
    {
      if (!dump)
	return proven;
      if (proven)
	{
	  if (derived_words || tu_leaned)
	    fprintf (dump, "Macro-planner ambient-walk: derived through"
		     " opaque init (%u raw words decoded"
		     " ambient-preserving, %u audited-TU asm)\n",
		     derived_words, tu_leaned);
	}
      else if (dirty_insn)
	{
	  fprintf (dump, "Macro-planner ambient-walk dirty: insn %d bb %d"
		   " (%s)", INSN_UID (dirty_insn),
		   BLOCK_FOR_INSN (dirty_insn)->index, dirty_why);
	  uint32_t w;
	  if (rvtt_raw_ttinsn_word (dirty_insn, &w))
	    fprintf (dump, " word=0x%08x", w);
	  fprintf (dump, "\n");
	}
      return proven;
    };

  int c = scan (point_bb, before);
  if (c)
    return report (c == 1);

  hash_set<basic_block> visited;
  auto_vec<basic_block, 16> work;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, point_bb->preds)
    work.safe_push (e->src);
  while (!work.is_empty ())
    {
      basic_block b = work.pop ();
      if (b == ENTRY_BLOCK_PTR_FOR_FN (cfun))
	continue;
      if (visited.add (b))
	continue;
      switch (scan (b, nullptr))
	{
	case 1:
	  continue;
	case 2:
	  return report (false);
	default:
	  FOR_EACH_EDGE (e, ei, b->preds)
	    work.safe_push (e->src);
	  break;
	}
    }
  return report (true);
}

/* Structural preheader of a loop-body region, with the zero-trip and
   whole-body ownership obligations (WP8).  The loop header must have
   exactly its backedge plus one external incoming edge; the incoming
   block must have no other successor, which proves at least one trip on
   this edge, so hoisting the all-lanes enable is not a zero-trip CC
   change; and every Tensix issue in the body must belong to the region,
   so no foreign issue can mutate configuration, CC, or counter state
   between the preheader materialization and any launch.  Refusal paths
   return null after dumping a stable name.  */

static basic_block
loop_region_preheader (function *fn, const macro_region &region, FILE *dump)
{
  basic_block body = region.bb;
  edge incoming = nullptr;
  unsigned self_edges = 0, external_edges = 0;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, body->preds)
    if (e->src == body)
      ++self_edges;
    else
      {
	++external_edges;
	incoming = e;
      }
  if (self_edges != 1 || external_edges != 1
      || incoming->src == ENTRY_BLOCK_PTR_FOR_FN (fn))
    {
      rvtt_refuse (RVTT_REF_LOOP_PREHEADER_UNPROVEN, dump,
		   "Macro-planner formation-refusal:"
		   " loop-preheader-unproven\n");
      return nullptr;
    }
  if (EDGE_COUNT (incoming->src->succs) != 1)
    {
      rvtt_refuse (RVTT_REF_ZERO_TRIP_PREHEADER_UNPROVEN, dump,
		   "Macro-planner formation-refusal:"
		   " zero-trip-preheader-unproven\n");
      return nullptr;
    }

  for (rtx_insn *insn = BB_HEAD (body); insn; insn = NEXT_INSN (insn))
    {
      if (NONDEBUG_INSN_P (insn) && recog_memoized (insn) >= 0
	  && get_attr_type (insn) == TYPE_TENSIX)
	{
	  bool owned = false;
	  for (const macro_row &row : region.rows)
	    {
	      owned |= insn == row.enable || insn == row.separator;
	      for (rtx_insn *member : row.insns)
		owned |= insn == member;
	    }
	  for (rtx_insn *sep : region.run_separators)
	    owned |= insn == sep;
	  if (!owned)
	    {
	      rvtt_refuse (RVTT_REF_LOOP_BODY_NOT_OWNED, dump,
			   "Macro-planner formation-refusal:"
			   " loop-body-not-owned\n");
	      return nullptr;
	    }
	}
      if (insn == BB_END (body))
	break;
    }
  return incoming->src;
}

/* Rewrite the typed address-mode operand of a copied explicit-load
   pattern to ADDR_MODE (WP10 compact CC calendar: the trailing load's
   own auto-increment mode absorbs the deleted separator's stride).
   The operand position mirrors rvtt_dst_access_operands' positional
   knowledge for the one admitted load pattern: rvtt_sfpload_lv_int
   carries operands 1..8 as unspec_volatile vector elements 0..7, so
   the addr_mode operand 8 is element 7.  Returns false -- without
   mutating -- for any other pattern; formation checks this BEFORE any
   emission so refusal paths never mutate.  */

static bool
planner_rewrite_load_addr_mode (rtx_insn *orig, rtx pat, unsigned addr_mode)
{
  if (recog_memoized (orig) != CODE_FOR_rvtt_sfpload_lv_int)
    return false;
  rtx set = GET_CODE (pat) == PARALLEL ? XVECEXP (pat, 0, 0) : pat;
  if (GET_CODE (set) != SET)
    return false;
  rtx src = SET_SRC (set);
  if (GET_CODE (src) != UNSPEC_VOLATILE || XVECLEN (src, 0) < 8)
    return false;
  if (pat != PATTERN (orig))	/* the copy, never the original */
    XVECEXP (src, 0, 7) = GEN_INT (addr_mode);
  return true;
}

/* Rewrite the typed Dst address immediate of a copied row pattern to
   NEW_ADDR (lane IS, F1 honest fix: immediate-delta rows normalize
   their explicit reloads back to rows[0]'s base -- the absorbed-stride
   calendar supplies the per-row advance through the counter).  The
   operand positions mirror rvtt_dst_access_operands' positional
   knowledge for the two admitted Dst patterns; the address element is
   unspec element 3 in both.  Returns false -- without mutating -- for
   any other shape; formation dry-runs this BEFORE any emission so
   refusal paths never mutate.  */

static bool
planner_rewrite_dst_address (rtx_insn *orig, rtx pat, HOST_WIDE_INT new_addr)
{
  int code = recog_memoized (orig);
  rtx unspec;
  if (code == CODE_FOR_rvtt_sfpload_lv_int)
    {
      rtx set = GET_CODE (pat) == PARALLEL ? XVECEXP (pat, 0, 0) : pat;
      if (GET_CODE (set) != SET)
	return false;
      unspec = SET_SRC (set);
    }
  else if (code == CODE_FOR_rvtt_sfpstore_int)
    unspec = GET_CODE (pat) == PARALLEL ? XVECEXP (pat, 0, 0) : pat;
  else
    return false;
  if (GET_CODE (unspec) != UNSPEC_VOLATILE || XVECLEN (unspec, 0) < 4
      || !CONST_INT_P (XVECEXP (unspec, 0, 3)))
    return false;
  if (pat != PATTERN (orig))	/* the copy, never the original */
    XVECEXP (unspec, 0, 3) = GEN_INT (new_addr);
  return true;
}

/* Insert the sequence SEQ at BB's tail (before a trailing jump), the
   compiler-owned insertion point after the last reachable foreign
   owner.  */

static void
insert_at_preheader_tail (rtx_insn *seq, basic_block bb)
{
  rtx_insn *tail = BB_END (bb);
  if (tail && JUMP_P (tail))
    emit_insn_before (seq, tail);
  else
    emit_insn_after (seq, tail);
}

/* Emit one run: the configuration prefix (first run only; hoisted to
   CONFIG_PREHEADER for a proven loop-body region; the descriptor-word
   part hoisted further to HOIST_PREHEADER under a proven cross-tile
   configuration epoch), the per-row issue calendar from the descriptor,
   and the drain; then delete the explicit rows.  Everything emitted is
   descriptor data.  */

/* Derive the launch's issue-plane effect record from the descriptor
   this planner invocation just synthesized (contract: rvtt-effects.h).
   MACRO_INDEX selects the launch's sequence word; VD is the actual
   (parity-resolved) launch VD index; HIDDEN the launch's hidden
   template-write mask; LMEM/SMEM the carried Dst memory operands.
   Every fact comes from the descriptor's own SequenceBits and the
   audited capability-table latency facts -- never from op names or
   instruction-word fingerprints.  Fails closed (no record): a
   CC-writing calendar (its loads are lane-predicated, outside the
   full-lane write contract), an out-of-range template index, or an
   undecodable byte.  VD16 staging events record an LREG16 write
   (bit 16 of the mask domain, handled by consumers exactly as every
   other insn's LREG16 effect).  */

static bool
derive_planner_launch_effects (const macro_descriptor &desc,
			       unsigned macro_index, unsigned vd,
			       uint32_t hidden, int addr_mode,
			       rtx lmem, rtx smem,
			       xtt_effect_set *out)
{
  using namespace rvtt_macro;

  if (desc.cc.active || macro_index >= desc.n_seq || vd >= 16
      || (hidden & ~0xFFFFu))
    return false;

  uint8_t bytes[4];
  decompose_sequence_word (desc.seq[macro_index], bytes);
  int settle = 0;
  uint32_t writes = (1u << vd) | hidden;
  for (unsigned u = 0; u != 4; ++u)
    {
      unsigned case_kind, delay;
      bool vd16, route_vb;
      if (!decode_sequence_bits (bytes[u], &case_kind, &delay, &vd16,
				 &route_vb))
	return false;
      if (case_kind == SEQ_CASE_SKIP || case_kind == SEQ_CASE_NOP)
	continue;
      if (case_kind >= SEQ_CASE_TEMPLATE0)
	{
	  if (case_kind - SEQ_CASE_TEMPLATE0 >= desc.n_templates)
	    return false;
	  /* A value event targets the launch VD, or LREG16 when its
	     VD16 flag is set (the staging register; bit 16 of the
	     vocabulary's L0..L15/LREG16 mask domain).  A store event's
	     VD16 flag is a READ of LREG16 -- no LREG write.  */
	  writes |= vd16 ? (1u << 16) : (1u << vd);
	}
      /* Event writeback completes at issue + 1 + delay +
	 subunit_result_latency; the launch's own done slot is
	 issue + 1, so the settle distance past done is
	 delay + subunit_result_latency.  */
      int done = (int) delay + (int) subunit_result_latency (u);
      if (done > settle)
	settle = done;
    }

  xtt_effect_set e = {};
  e.opaque = false;
  e.subunit = XTT_SU_LOAD;
  e.lreg_read = 0;		/* issue-plane: never operand-gated */
  e.lreg_write = writes;
  e.result_latency = settle;
  e.next_slot_stall = false;
  /* Address-mode RWC effect, the same capability fact the sfpload
     ADDR_MODE class resolves against: the no-increment mode is NONE;
     auto-increment deltas stay UNKNOWN (capability-table data).  */
  int no_inc = rvtt_no_increment_address_mode ();
  e.rwc.kind = (no_inc >= 0 && addr_mode == no_inc
		? xtt_rwc_effect_t::NONE : xtt_rwc_effect_t::UNKNOWN);
  e.dst_mem_read = lmem && MEM_P (lmem);
  e.dst_mem_write = smem && MEM_P (smem);
  *out = e;
  return true;
}

static void
emit_planner_run (macro_region &region, const macro_schedule &schedule,
		  const macro_descriptor &desc,
		  const rvtt_macro::caps *c,
		  unsigned begin, unsigned end, bool emit_config,
		  basic_block config_preheader, rtx_insn *enable_src,
		  basic_block hoist_preheader, rtx_insn *hoist_enable_src,
		  bool emit_drain,
		  /* Lane EV (P0 wrong-code fix, 2026-08-21): place this
		     many drain NOPs between consecutive rows of this
		     run -- the FULL derived drain when a fixed-VD VALUE
		     carrier's hosted events pend past the next row's
		     launch (see form_region for the derivation and
		     provenance), or the smaller residual lane FT's
		     window-pairing tuning proved
		     (rvtt_macro_interrow_drain_tuned, under
		     -mtt-tensix-optimize-window-pairing).  */
		  int interrow_drain_slots,
		  /* WP13 residency (rvtt-macro-desc.cc): elide the
		     descriptor words when a bit-identical dominating
		     resident program exists; collect the programming
		     insns (benign for later residency walks); report
		     where the words were programmed.  */
		  bool resident_elide, macro_residency_state *resid,
		  /* Lane CA cross-call init hoist: 0 = none, 1 = the
		     descriptor words live in the caller's preheader
		     (retain enable + owned SETC16 per call), 2 = the
		     full prefix lives there (emit nothing).  */
		  int init_hoist_stage,
		  basic_block *config_placement)
{
  const macro_row &first = region.rows[begin];
  rtx_insn *anchor = first.enable ? first.enable : first.insns[0];

  /* Record every insn of SEQ as planner-emitted (residency-benign).  */
  auto collect_emitted = [&] (rtx_insn *seq)
    {
      if (resid)
	for (rtx_insn *i = seq; i; i = NEXT_INSN (i))
	  resid->emitted.add (i);
    };

  if (emit_config)
    {
      /* The all-lanes proof source ENABLE_SRC is the first row's local
	 enable, or (WP10) the first row's own proven all-lanes restore
	 materialized in the prefix -- both proven word-exact all-lanes
	 by formation (cc_enable_all_lanes_proved_p), so this copy
	 re-establishes exactly the proven state; the WP8 relaxation
	 from every-row holds because no region member may write CC
	 outside the admitted CC-template roles, whose only lane-state
	 net effect is the proven all-lanes restore.  A null ENABLE_SRC
	 is the loop preheader's own trailing enable (proven all-lanes;
	 already in place; no copy).  */
      rtx config_lreg = gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST);
      auto config_word = [&] (uint32_t word, unsigned dest)
	{
	  rvtt_emit_sfpxloadi (config_lreg, rvtt_gen_rtx_noval (XTT32SImode),
			       GEN_INT (word));
	  emit_insn (gen_rvtt_sfpwriteconfig_v (config_lreg,
						GEN_INT (dest)));
	};
      auto emit_config_words = [&] ()
	{
	  for (unsigned t = 0; t != desc.n_templates; ++t)
	    config_word (desc.templ[t], t);
	  for (unsigned m = 0; m != desc.n_seq; ++m)
	    config_word (desc.seq[m], 4 + m);
	  if (desc.has_misc)
	    config_word (desc.misc, 8);
	};

      if (init_hoist_stage == 2)
	{
	  /* The whole prefix is resident in the caller's loop
	     preheader under the committed cross-call contract; nothing
	     to establish per call.  */
	  if (config_placement)
	    *config_placement = nullptr;
	}
      else if (init_hoist_stage == 1)
	{
	  /* Descriptor words live in the caller's preheader; the
	     ambient enable and the owned SETC16 program stay per call
	     (the stage-1 contract).  */
	  start_sequence ();
	  if (enable_src)
	    emit_insn (copy_rtx (PATTERN (enable_src)));
	  for (unsigned sx = 0; sx != desc.n_setc16; ++sx)
	    emit_insn (gen_rvtt_owned_setc16
		       (GEN_INT (desc.setc16[sx].config_reg),
			GEN_INT (desc.setc16[sx].value)));
	  rtx_insn *retained = get_insns ();
	  end_sequence ();
	  collect_emitted (retained);
	  emit_insn_before (retained, anchor);
	  if (config_placement)
	    *config_placement = nullptr;
	}
      else if (resident_elide)
	{
	  /* WP13 residency: the descriptor words are already resident
	     (a bit-identical program at a proven dominating placement
	     under function-wide owned-state invariance) -- only the
	     per-region ambient enable and owned SETC16 program are
	     re-established.  */
	  start_sequence ();
	  if (enable_src)
	    emit_insn (copy_rtx (PATTERN (enable_src)));
	  for (unsigned s = 0; s != desc.n_setc16; ++s)
	    emit_insn (gen_rvtt_owned_setc16
		       (GEN_INT (desc.setc16[s].config_reg),
			GEN_INT (desc.setc16[s].value)));
	  rtx_insn *retained = get_insns ();
	  end_sequence ();
	  collect_emitted (retained);
	  if (config_preheader)
	    insert_at_preheader_tail (retained, config_preheader);
	  else
	    emit_insn_before (retained, anchor);
	  if (config_placement)
	    *config_placement = nullptr;	/* nothing newly programmed */
	}
      else if (hoist_preheader)
	{
	  /* Cross-tile configuration epoch (WP11): the descriptor words
	     execute once, in the enclosing loop's structural preheader
	     -- the epoch proof shows no intervening owner, so every
	     trip's launches read exactly these words.  The block is
	     self-sufficient under lane masking: the copied proven
	     all-lanes enable precedes the lane-predicated LREG
	     materialization, under the same outermost-CC-depth license
	     as the per-trip materialized enable.  */
	  start_sequence ();
	  emit_insn (copy_rtx (PATTERN (hoist_enable_src)));
	  emit_config_words ();
	  rtx_insn *hoisted = get_insns ();
	  end_sequence ();
	  collect_emitted (hoisted);
	  insert_at_preheader_tail (hoisted, hoist_preheader);
	  if (config_placement)
	    *config_placement = hoist_preheader;

	  /* Retained per-trip prefix: the ambient enable (the calendar's
	     entry lane state is re-established every tile) and the owned
	     SETC16 address-modifier program (SETC16-visible state stays
	     inside the per-tile discipline; the epoch proof does not
	     cover data-plane MMIO writes to it).  */
	  start_sequence ();
	  if (enable_src)
	    emit_insn (copy_rtx (PATTERN (enable_src)));
	  for (unsigned s = 0; s != desc.n_setc16; ++s)
	    emit_insn (gen_rvtt_owned_setc16
		       (GEN_INT (desc.setc16[s].config_reg),
			GEN_INT (desc.setc16[s].value)));
	  rtx_insn *retained = get_insns ();
	  end_sequence ();
	  collect_emitted (retained);
	  if (config_preheader)
	    insert_at_preheader_tail (retained, config_preheader);
	  else
	    emit_insn_before (retained, anchor);
	}
      else
	{
	  start_sequence ();
	  if (enable_src)
	    emit_insn (copy_rtx (PATTERN (enable_src)));
	  for (unsigned s = 0; s != desc.n_setc16; ++s)
	    emit_insn (gen_rvtt_owned_setc16
		       (GEN_INT (desc.setc16[s].config_reg),
			GEN_INT (desc.setc16[s].value)));
	  emit_config_words ();
	  rtx_insn *prefix = get_insns ();
	  end_sequence ();
	  collect_emitted (prefix);
	  if (config_preheader)
	    {
	      /* Loop-body region: the prefix executes once, in the proven
		 structural preheader (>= one trip; see
		 loop_region_preheader).  */
	      insert_at_preheader_tail (prefix, config_preheader);
	      if (config_placement)
		*config_placement = config_preheader;
	    }
	  else
	    {
	      emit_insn_before (prefix, anchor);
	      if (config_placement)
		*config_placement = BLOCK_FOR_INSN (anchor);
	    }
	}
    }

  /* Per-macro carried memory operands and hidden template writes.  */
  rtx carrier_load_mem[4] = {}, carrier_store_mem[4] = {};
  uint32_t carrier_hidden[4] = {};
  const macro_row &row0 = region.rows[0];
  for (unsigned ix = 0; ix != row0.insns.length (); ++ix)
    {
      const macro_event &ev = schedule.events[ix];
      xtt_effect_set e = rvtt_insn_effects (row0.insns[ix]);
      rtx address, mode, addr_mode;
      if (ev.is_carrier && e.dst_mem_read
	  && rvtt_dst_access_operands (row0.insns[ix], e, &address, &mode,
				       &addr_mode))
	{
	  extract_insn (row0.insns[ix]);
	  carrier_load_mem[ev.macro_index] = recog_data.operand[1];
	}
      if (ev.realization == macro_event::LAUNCHED_TEMPLATE_SLOT
	  && ev.is_store)
	{
	  extract_insn (row0.insns[ix]);
	  carrier_store_mem[ev.macro_index] = recog_data.operand[0];
	}
      if (ev.realization == macro_event::LAUNCHED_TEMPLATE_SLOT
	  && !ev.is_store && ev.template_id < desc.n_templates)
	carrier_hidden[ev.macro_index]
	  |= rvtt_macro::template_hidden_lreg_writes
	       (c, desc.templ[ev.template_id]);
    }

  /* Planned destination of each explicit reload: the src field of the
     template consuming its value (decoded from the descriptor), or --
     for a load feeding the coalesced lane-merge (WP9) -- the shared
     launch VD the predicated-overwrite dataflow flows through.  */
  unsigned explicit_planned[8] = {};
  bool explicit_planned_valid[8] = {};
  for (unsigned ix = 0; ix != row0.insns.length (); ++ix)
    {
      const macro_event &ev = schedule.events[ix];
      xtt_effect_set e = rvtt_insn_effects (row0.insns[ix]);
      if (ev.realization != macro_event::EXPLICIT_INSN || !e.dst_mem_read
	  || ev.is_carrier || ix >= 8)
	continue;
      explicit_planned[ix] = 0;
      for (unsigned jx = 0; jx != row0.insns.length (); ++jx)
	{
	  const macro_event &cons = schedule.events[jx];
	  if (cons.realization == macro_event::CC_COALESCED)
	    {
	      xtt_effect_set ce = rvtt_insn_effects (row0.insns[jx]);
	      if ((ce.lreg_read & e.lreg_write)
		  && !desc.launches.is_empty ())
		{
		  explicit_planned[ix] = desc.launches[0].vd;
		  explicit_planned_valid[ix] = true;
		}
	      continue;
	    }
	  if (cons.realization != macro_event::LAUNCHED_TEMPLATE_SLOT
	      || cons.is_store || cons.template_id >= desc.n_templates)
	    continue;
	  xtt_effect_set ce = rvtt_insn_effects (row0.insns[jx]);
	  if (!(ce.lreg_read & e.lreg_write))
	    continue;
	  rvtt_macro::template_spec spec;
	  if (rvtt_macro::decode_template (desc.templ[cons.template_id],
					   &spec)
	      && spec.src_c)
	    {
	      explicit_planned[ix] = spec.src_c;
	      explicit_planned_valid[ix] = true;
	    }
	}
    }

  start_sequence ();
  for (unsigned r = begin; r != end; ++r)
    {
      const macro_row &row = region.rows[r];
      unsigned parity = (r - begin) & 1;
      /* Issue order follows the schedule slots: carriers and explicit
	 reloads interleave exactly as derived.  */
      for (int slot = 0; slot != schedule.ii; ++slot)
	for (unsigned ix = 0; ix != row.insns.length (); ++ix)
	  {
	    const macro_event &ev = schedule.events[ix];
	    if (!ev.issues_word || ev.slot != slot)
	      continue;
	    if (ev.is_carrier)
	      {
		const macro_launch_spec *launch = nullptr;
		for (const macro_launch_spec &ls : desc.launches)
		  if (ls.macro_index == ev.macro_index)
		    launch = &ls;
		if (!launch)
		  continue;
		uint32_t word = launch->vd_alternates && parity
		  ? launch->word_alt : launch->word;
		unsigned vd = launch->vd_alternates && parity
		  ? launch->vd ^ 1 : launch->vd;
		rtx vd_reg = gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST + vd);
		rtx lmem = carrier_load_mem[ev.macro_index];
		rtx smem = carrier_store_mem[ev.macro_index];
		rtx mem1 = lmem ? lmem : smem;
		rtx mem2 = smem ? smem : const0_rtx;
		if (!smem)
		  mem2 = const0_rtx;
		uint32_t hidden = carrier_hidden[ev.macro_index];
		rtx_insn *launch_insn;
		if (hidden)
		  {
		    unsigned hreg = ctz_hwi (hidden);
		    launch_insn
		      = emit_insn (gen_rvtt_sfploadmacro_hidden_int
				   (vd_reg, mem1, mem2,
				    GEN_INT (launch->address),
				    GEN_INT (launch->mode),
				    GEN_INT (launch->addr_mode),
				    GEN_INT (word),
				    gen_rtx_REG (XTT32SImode,
						 SFPU_REG_FIRST + hreg)));
		  }
		else
		  launch_insn
		    = emit_insn (gen_rvtt_sfploadmacro_int
				 (vd_reg, mem1, mem2 == const0_rtx && smem
				  ? smem : mem2,
				  GEN_INT (launch->address),
				  GEN_INT (launch->mode),
				  GEN_INT (launch->addr_mode),
				  GEN_INT (word)));
		/* Planner emission record (rvtt-effects.h): the launch's
		   issue-plane effect interface, derived from the
		   descriptor just synthesized.  Fail-closed: a refused
		   derivation leaves the launch effect-opaque exactly as
		   before.  */
		xtt_effect_set launch_fx;
		if (derive_planner_launch_effects (desc, ev.macro_index,
						   vd, hidden,
						   launch->addr_mode,
						   lmem, smem, &launch_fx))
		  rvtt_planner_launch_effects_record
		    (launch_insn, word, SFPU_REG_FIRST + vd, launch_fx);
	      }
	    else
	      {
		/* Explicit reload retargeted to its planned register.  */
		rtx pat = copy_rtx (PATTERN (region.rows[r].insns[ix]));
		if (ix < 8 && explicit_planned_valid[ix])
		  {
		    rtx set = GET_CODE (pat) == PARALLEL
		      ? XVECEXP (pat, 0, 0) : pat;
		    if (GET_CODE (set) == SET)
		      SET_DEST (set)
			= gen_rtx_REG (XTT32SImode,
				       SFPU_REG_FIRST + explicit_planned[ix]);
		  }
		/* WP10 compact CC calendar: the trailing explicit load
		   absorbs the deleted separator's Dst stride through the
		   tables' owned auto-increment address-modifier slot
		   (the SETC16 programs in the configuration prefix own
		   its meaning).  Formation proved the operand rewrite
		   possible before any mutation.  */
		if (ev.absorbs_stride)
		  {
		    bool ok = planner_rewrite_load_addr_mode
		      (region.rows[r].insns[ix], pat,
		       c->auto_increment_dst2_addr_mode);
		    gcc_assert (ok);
		  }
		/* Immediate-delta row (lane IS, F1 honest fix):
		   normalize the copied explicit Dst access back to
		   rows[0]'s base -- the absorbed calendar's counter
		   supplies the per-row advance.  Formation dry-ran the
		   rewrite on every Dst access of every offset row.  */
		if (region.imm_stride && region.rows[r].imm_delta)
		  {
		    xtt_effect_set fe
		      = rvtt_insn_effects (region.rows[r].insns[ix]);
		    if (fe.dst_mem_read || fe.dst_mem_write)
		      {
			rtx address, mode, am;
			bool ok = rvtt_dst_access_operands
			  (region.rows[r].insns[ix], fe, &address, &mode,
			   &am)
			  && CONST_INT_P (address)
			  && planner_rewrite_dst_address
			       (region.rows[r].insns[ix], pat,
				INTVAL (address)
				- region.rows[r].imm_delta);
			gcc_assert (ok);
		      }
		  }
		emit_insn (pat);
	      }
	  }
      /* WP9: the proven CC-template program keeps the row's typed
	 separator in place -- its issue slot is the restore's
	 visibility slot (macro_cc_model), so the next row opens under
	 the restored all-lanes mask.  Re-emitted verbatim.  */
      if (desc.keep_separator && row.separator)
	emit_insn (copy_rtx (PATTERN (row.separator)));
      /* Lane EV inter-row drain: between consecutive rows only (the
	 run-end drain below still owns the run boundary).  Reproduces
	 byte-for-byte the proven rolled per-row calendar -- launch
	 followed by the full derived drain -- whose issue-stream
	 spacing is the proven envelope (H2: stream slots lower-bound
	 issue-cycle distance).  */
      if (r + 1 != end)
	for (int d = 0; d != interrow_drain_slots; ++d)
	  emit_insn (gen_rvtt_sfpnop ());
    }
  /* The derived drain (core_drain_slots over the descriptor's own
     SequenceBits delays).  Under -mtt-tensix-optimize-drain-schedule an
     intra-region run boundary whose follower stream provably cannot
     conflict with the in-flight events elides it
     (rvtt_macro_drain_boundary_elidable, rtl-rvtt-schedule.cc); every
     refusal and the final run keep it byte-identically.  */
  if (emit_drain)
    for (int d = 0; d != desc.drain_slots; ++d)
      emit_insn (gen_rvtt_sfpnop ());
  rtx_insn *replacement = get_insns ();
  end_sequence ();
  /* The emitted calendar (launches, explicit reloads, separators,
     drain) is planner-emitted and benign for the WP13 residency walks
     by construction -- launch effects are deliberately opaque to the
     effect vocabulary (descriptor-dependent), so without this the
     walks would refuse on our own launches.  Collected BEFORE
     insertion (the sequence walk must end at the sequence).  */
  if (resid)
    for (rtx_insn *i = replacement; i; i = NEXT_INSN (i))
      resid->emitted.add (i);
  emit_insn_before (replacement, anchor);

  for (unsigned r = begin; r != end; ++r)
    {
      const macro_row &row = region.rows[r];
      if (row.enable)
	delete_insn (row.enable);
      for (rtx_insn *insn : row.insns)
	delete_insn (insn);
      if (row.separator)
	delete_insn (row.separator);
    }
}

/* Lane CA cross-call init hoist, callee side: every instruction of FN
   outside REGION must be proven unable to disturb the hoisted state or
   depend on the per-call prefix -- no call, no unaudited asm or Tensix
   instruction, no CC write other than the proven all-lanes enable
   class, no configuration or LREG or Dst effect, no scalar memory
   store (the delivered-word idiom).  Pure-RWC counter words and plain
   scalar register/branch code are neutral.  Returns the refusing insn
   through *WHY_INSN, or nullptr when clean.  */

static const char *
init_hoist_callee_scan (function *fn, const macro_region &region,
			rtx_insn **why_insn)
{
  *why_insn = nullptr;
  hash_set<rtx_insn *> members;
  for (const macro_row &row : region.rows)
    {
      if (row.enable)
	members.add (row.enable);
      if (row.separator)
	members.add (row.separator);
      for (rtx_insn *member : row.insns)
	members.add (member);
    }
  for (rtx_insn *sep : region.run_separators)
    members.add (sep);

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn) || members.contains (insn))
	    continue;
	  rtx pat = PATTERN (insn);
	  if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	    continue;
	  *why_insn = insn;
	  if (CALL_P (insn))
	    return "drain-init-callee-unproven";
	  xtt_effect_set e = rvtt_insn_effects (insn);
	  if (!e.opaque)
	    {
	      if (e.cc_write && e.cc_write_all_lanes && !e.cc_read
		  && !e.lreg_read && !e.lreg_write
		  && !e.config_dests_written && !e.config_dests_read
		  && !e.addr_mod_slot_write
		  && !e.dst_mem_read && !e.dst_mem_write)
		continue;	/* re-establishes the contract state */
	      if (e.cc_read || e.cc_write
		  || e.config_dests_written || e.config_dests_read
		  || e.addr_mod_slot_write
		  || e.lreg_read || e.lreg_write
		  || e.dst_mem_read || e.dst_mem_write)
		return "drain-init-callee-unproven";
	      continue;		/* pure-RWC counter class */
	    }
	  if (asm_noperands (pat) >= 0)
	    return "drain-init-callee-unproven";
	  if (recog_memoized (insn) >= 0
	      && get_attr_type (insn) == TYPE_TENSIX)
	    return "drain-init-callee-unproven";
	  bool stores_mem = false;
	  auto note_mem = [] (rtx x, const_rtx, void *data)
	    {
	      if (MEM_P (x))
		*(bool *) data = true;
	    };
	  note_stores (insn, note_mem, &stores_mem);
	  if (stores_mem)
	    return "drain-init-callee-unproven";
	}
    }
  *why_insn = nullptr;
  return nullptr;
}

/* Form REGION when every proof holds; returns true when code changed.
   Refusal paths never mutate.  */

static bool
form_region (function *fn, macro_region &region,
	     const macro_schedule &schedule, const macro_descriptor &desc,
	     macro_residency_state *resid, bool sole_region, FILE *dump)
{
  rvtt_macro::cpu_t cpu = TARGET_XTT_TENSIX_BH ? rvtt_macro::CPU_BH
    : TARGET_XTT_TENSIX_WH ? rvtt_macro::CPU_WH : rvtt_macro::CPU_QSR;
  const rvtt_macro::caps *c = rvtt_macro_caps_for_cpu (cpu);
  if (!c || desc.refusal || desc.drain_slots < 0
      || desc.launches.is_empty ())
    return false;

  /* Configuration ownership: the function-global proof, or the ordered
     region-scoped fallbacks (union merge, WP9 x cross-function
     regions): (1) any loop-body region first tries the loop-scoped
     preheader+body WINDOW proof (the cheaper, more general path -- the
     preheader is computed quietly here; the window dump line names the
     sharing); (2) a proven CC-template program that the window path
     did not prove -- including the straight-line shapes the window
     never covers -- additionally tries the WP9 CC-scoped proof
     (planner_region_config_ownership_ok; no success dump line, as
     before).  Only when every applicable proof fails does the refusal
     keep its established name.  Configuration placement is decided
     once: scoped_preheader is set only by the window path, and the
     loop-preheader recomputation below covers the CC-scoped loop
     case, so the prefix is never double-placed.  */
  basic_block scoped_preheader = nullptr;
  if (!planner_config_ownership_ok (fn, c))
    {
      if (region.loop_body)
	{
	  scoped_preheader = loop_region_preheader (fn, region, nullptr);
	  if (scoped_preheader && !planner_config_window_ok (region))
	    scoped_preheader = nullptr;
	}
      bool cc_scoped_ok = false;
      if (!scoped_preheader && desc.cc.active)
	{
	  basic_block cc_preheader = region.loop_body
	    ? loop_region_preheader (fn, region, nullptr) : nullptr;
	  if (!region.loop_body || cc_preheader)
	    {
	      rtx_insn *anchor = region.rows[0].enable
		? region.rows[0].enable : region.rows[0].insns[0];
	      cc_scoped_ok = planner_region_config_ownership_ok
		(region, cc_preheader, anchor, c);
	    }
	}
      if (!scoped_preheader && !cc_scoped_ok)
	{
	  rvtt_refuse (RVTT_REF_CONFIG_OWNERSHIP_UNPROVEN, dump,
		       "Macro-planner formation-refusal:"
		       " config-ownership-unproven\n");
	  return false;
	}
      if (scoped_preheader && dump)
	fprintf (dump, "Macro-planner config-ownership: loop-scoped"
		 " window (preheader tail dominates every launch)\n");
    }

  /* Every planner-owned physical LREG must be dead after the region.  */
  rtx_insn *region_end = region.rows.last ().separator
    ? region.rows.last ().separator : region.rows.last ().insns.last ();
  for (unsigned reg = 0; reg != 17; ++reg)
    if ((desc.planned_lregs >> reg) & 1)
      if (!planned_value_dead_after_p
	    (gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST + reg), region_end))
	{
	  rvtt_refuse (RVTT_REF_PLANNED_LREG_LIVE, dump,
		       "Macro-planner formation-refusal:"
		       " planned-lreg-live\n");
	  return false;
	}

  auto_vec<unsigned> run_begins;
  for (unsigned r = 0; r != region.rows.length (); ++r)
    if (region.rows[r].starts_run)
      run_begins.safe_push (r);
  if (run_begins.is_empty ())
    run_begins.safe_push (0);

  /* A loop-body region hoists its configuration to the structural
     preheader; that placement carries the zero-trip and whole-body
     ownership obligations.  */
  basic_block config_preheader = nullptr;
  gcov_type body_count = 1, preheader_count = 1;
  if (region.loop_body)
    {
      config_preheader = scoped_preheader
	? scoped_preheader : loop_region_preheader (fn, region, dump);
      if (!config_preheader)
	return false;
      if (!loop_trip_weight (region.bb, config_preheader, &body_count,
			     &preheader_count))
	{
	  /* No usable trip estimate: conservatively a single trip.  */
	  body_count = preheader_count = 1;
	}
    }

  /* Every ambient enable the formation consumes -- each row's local
     enable (all are deleted and one is re-emitted in the prefix) and
     the loop preheader's trailing enable -- must provably write the
     all-lanes state: the proven store/misc envelope covers no partial
     lane mask, and the deleted quarantined pass refused exactly here.
     Region discovery only admits proven enables; this re-check keeps
     the formation contract locally auditable and guards every future
     discovery widening.  */
  for (const macro_row &row : region.rows)
    if (row.enable && !cc_enable_all_lanes_proved_p (row.enable))
      {
	rvtt_refuse (RVTT_REF_CC_ENABLE_UNPROVED, dump,
		     "Macro-planner formation-refusal:"
		     " cc-enable-unproved\n");
	return false;
      }

  /* A lane-predicated calendar needs the ambient all-lanes proof: the
     region's first row's local enable (WP8 relaxation from every-row:
     region members cannot write CC -- such rows refuse
     cc-template-unsupported at discovery -- so the entry lane state
     holds across every row), or, for a loop-body region whose enable
     was written once outside the loop, the proven preheader's trailing
     enable (whole-body ownership keeps it live across every trip).  */
  rtx_insn *enable_src = region.rows[0].enable;
  bool materialized_enable = false;
  bool ambient_enable = false;
  if (desc.needs_all_lanes_prefix && !region.rows[0].enable)
    {
      rtx_insn *trailing = config_preheader
	? preheader_trailing_enable (config_preheader) : nullptr;
      if (trailing)
	{
	  if (!cc_enable_all_lanes_proved_p (trailing))
	    {
	      /* A trailing pure CC write exists but its written lane
		 state is not provably the all-lanes pattern (lanes-off,
		 partial mask, complement, ...): name the unproved
		 enable.  */
	      rvtt_refuse (RVTT_REF_CC_ENABLE_UNPROVED, dump,
			   "Macro-planner formation-refusal:"
			   " cc-enable-unproved\n");
	      return false;
	    }
	  enable_src = nullptr;	/* already in place; no copy */
	}
      else
	{
	  /* Materialized enable (WP10, superseding the WP9 first-row
	     peel): when no typed ambient enable exists -- the real LLK
	     kernels establish the lane state through opaque init the
	     typed IR cannot see -- a CC-template row's OWN all-lanes
	     restore is the proof source, and the formation MATERIALIZES
	     that proven word (a pattern copy, all-lanes word-exact
	     through the P0 sfpencc derivation) at the head of the
	     configuration prefix instead of executing the whole first
	     row explicitly.  The license is the compiler's own
	     established outermost-CC-depth contract: the row's
	     SETCC/.../ENCC combine is produced by rvtt_cc's
	     outermost-depth transform (gimple-rvtt-cc.cc), which
	     already replaces the outermost POPC (restore the pushed
	     state) with ENCC (enable all lanes) -- sound exactly
	     because the architectural kernel convention pins the
	     outermost lane state to all-lanes.  The materialized word
	     re-writes the state that contract already guarantees, so
	     the first row -- like every later row, inductively through
	     the launched restore template -- executes under the
	     all-lanes entry state.  Rows without an in-row proven
	     restore keep the named refusal.  */
	  rtx_insn *proof_restore = nullptr;
	  if (desc.cc.active && region.rows.length () > 1)
	    for (rtx_insn *member : region.rows[0].insns)
	      {
		xtt_effect_set e = rvtt_insn_effects (member);
		if (e.cc_write && !e.cc_read && !e.lreg_read
		    && !e.lreg_write && e.cc_write_all_lanes)
		  proof_restore = member;
	      }
	  if (proof_restore)
	    {
	      enable_src = proof_restore;
	      materialized_enable = true;
	    }
	  /* Entry-ambient derivation (lane IS, owner-ratified F1 honest
	     fix, 2026-08-29): when no typed enable exists anywhere --
	     the source carries no marker instruction -- the compiler
	     derives the lane state itself.  The license is the SAME
	     architectural contract every established consumer already
	     stands on (rvtt_cc's outermost POPC -> ENCC rewrite, the
	     WP10 materialization above, crossrow-pairing's loop-entry
	     walk, prgm-const's pre-peel walk): the structured-CC
	     lowering pins the function-entry and outermost lane state
	     to the architectural all-lanes state.  The derivation is a
	     kill-aware backwards CFG walk from the configuration
	     placement point: every path must reach the function entry
	     or a word-exact all-lanes SFPENCC before any other
	     CC-affecting, unaudited, or opaque instruction.  Raw asm is
	     seen THROUGH, never trusted (lane IV, typecast recovery):
	     decoded `.ttinsn' words classify against the audited
	     lane-enable table and the rest leans on the TU-wide
	     CC/lane-enable audit; calls and anything undecodable stay
	     DIRTY fail-closed (entry_ambient_all_lanes_p header
	     comment).  On success the formation
	     SYNTHESIZES the canonical all-lanes enable
	     (rvtt_sfpencc_all_lanes, the capability-table word, the
	     same word the crosscall init hoist already synthesizes
	     caller-side) at the head of the configuration prefix:
	     re-writing the state the derivation just proved -- a
	     machine-state no-op, priced as the one pushed word it
	     is.  An unproven walk keeps the named refusal.  */
	  else if (entry_ambient_all_lanes_p
		     (config_preheader
		      ? config_preheader
		      : BLOCK_FOR_INSN (region.rows[0].insns[0]),
		      config_preheader
		      ? nullptr : region.rows[0].insns[0], dump))
	    {
	      start_sequence ();
	      emit_insn (gen_rvtt_sfpencc_all_lanes ());
	      rtx_insn *synth = get_insns ();
	      end_sequence ();
	      gcc_assert (synth && !NEXT_INSN (synth)
			  && cc_enable_all_lanes_proved_p (synth));
	      enable_src = synth;
	      materialized_enable = true;
	      ambient_enable = true;
	      if (dump)
		fprintf (dump, "Macro-planner formation: entry-ambient"
			 " all-lanes derived (structured-CC fn-entry"
			 " contract); canonical enable synthesized\n");
	    }
	  else
	    {
	      rvtt_refuse (RVTT_REF_ALL_LANES_PROOF_MISSING, dump,
			   "Macro-planner formation-refusal:"
			   " all-lanes-proof-missing"
			   " (ambient-entry-unproven)\n");
	      return false;
	    }
	}
    }

  /* WP13 residency de-duplication (rvtt-macro-desc.cc): when a
     bit-identical descriptor program is already resident at a proven
     dominating placement under function-wide owned-state invariance,
     this region elides its descriptor-word programming entirely.
     Proof-only; computed here (lane IU) so the init-hoist pricing
     pre-run below sees the same eligibility the commit site keeps.
     Refusals keep today's emission byte-identically.  */
  bool resident_elide
    = rvtt_macro_residency_lookup (fn, region, desc, c, resid, dump);

  /* Init-hoist pricing pre-run (lane IU, 2026-08-29): the crosscall
     init hoist's proof chain, evaluated PROOF-ONLY ahead of the
     profitability gate under exactly the guards the committing site
     (below, still last among the refusal points) applies.  A proven
     stage-2 contract means the configuration prefix will leave the
     callee entirely -- the init contract words price ONCE per proven
     caller-loop entry, not per run (rvtt-cost.md caller-loop prefix
     amortization).  Nothing is inserted here; the commit only happens
     after every later refusal point has passed, and a refusal
     anywhere keeps caller and callee byte-identical.  */
  int init_hoist_stage = 0;
  rvtt_init_hoist_program init_prog = {};
  const char *init_refusal = nullptr;
  rtx_insn *init_refusal_insn = nullptr;
  if (riscv_tt_opt_init_hoist && !region.loop_body && !resident_elide)
    {
      if (!sole_region)
	init_refusal = "drain-init-callee-unproven";
      else if (!enable_src || (materialized_enable && !ambient_enable)
	       || !cc_enable_all_lanes_proved_p (enable_src)
	       || recog_memoized (enable_src) != CODE_FOR_rvtt_sfpencc)
	/* v1: the prefix's lane proof must be the typed proven
	   all-lanes SFPENCC (the minmax-class ambient enable); the
	   WP10 in-row-restore materialization license is not carried
	   cross-call.  The entry-ambient SYNTHESIZED enable (F1 honest
	   fix) is admitted: it is the same canonical word the hoist's
	   caller-side emission synthesizes itself
	   (rvtt_sfpencc_all_lanes), licensed by the derived fn-entry
	   ambient fact rather than a row-local restore.  */
	init_refusal = "drain-init-idempotence-unproven";
      else if (desc.n_setc16 > 8
	       || desc.n_templates + desc.n_seq + (desc.has_misc ? 1 : 0)
		  > 16)
	init_refusal = "drain-init-idempotence-unproven";
      else
	init_refusal = init_hoist_callee_scan (fn, region,
					       &init_refusal_insn);
      if (!init_refusal)
	{
	  init_prog.n_setc16 = desc.n_setc16;
	  for (unsigned i = 0; i != desc.n_setc16; ++i)
	    {
	      init_prog.setc16[i].reg = desc.setc16[i].config_reg;
	      init_prog.setc16[i].value = desc.setc16[i].value;
	    }
	  init_prog.n_words = 0;
	  for (unsigned t = 0; t != desc.n_templates; ++t)
	    {
	      init_prog.words[init_prog.n_words].word = desc.templ[t];
	      init_prog.words[init_prog.n_words++].dest = t;
	    }
	  for (unsigned m = 0; m != desc.n_seq; ++m)
	    {
	      init_prog.words[init_prog.n_words].word = desc.seq[m];
	      init_prog.words[init_prog.n_words++].dest = 4 + m;
	    }
	  if (desc.has_misc)
	    {
	      init_prog.words[init_prog.n_words].word = desc.misc;
	      init_prog.words[init_prog.n_words++].dest = 8;
	    }
	  init_refusal = rvtt_crosscall_init_hoist (fn, &init_prog,
						    /*commit=*/false);
	  if (!init_refusal)
	    {
	      init_hoist_stage = init_prog.stage;
	      if (dump)
		fprintf (dump, "Macro-planner init-hoist pricing"
			 " pre-run: stage=%d proven (weight=%lld/%lld"
			 " usable=%d)\n", init_hoist_stage,
			 (long long) init_prog.caller_body_count,
			 (long long) init_prog.caller_entry_count,
			 (int) init_prog.caller_weight_ok);
	    }
	}
      if (init_refusal && dump)
	{
	  rvtt_refuse_by_name (init_refusal, dump,
			       "Macro-planner init-hoist-refusal: %s",
			       init_refusal);
	  if (init_refusal_insn)
	    fprintf (dump, " (insn %d)", INSN_UID (init_refusal_insn));
	  fprintf (dump, "\n");
	}
    }
  /* The pricing sees the amortization only under the full (stage-2)
     contract with a usable profile weight; anything less keeps the
     frozen conservative-per-run discipline.  */
  int ih_pricing_stage
    = init_hoist_stage >= 2 && init_prog.caller_weight_ok
      ? init_hoist_stage : 0;

  /* Profitability.  Straight-line: every run independently amortizes
     the full configuration prefix (the frozen conservative-per-run
     discipline), except under the proven full init-hoist contract,
     where the prefix amortizes over the caller loop (above).  Loop
     body: the prefix is paid once in the preheader and weighted
     against the profile trip estimate.  */
  if (region.loop_body)
    {
      if (!loop_profitable_p (region, schedule, desc, body_count,
			      preheader_count, run_begins.length ()))
	{
	  if (dump)
	    fprintf (dump, "Macro-planner formation-refusal:"
		     " unprofitable (trip-weight=%ld/%ld)\n",
		     (long) body_count, (long) preheader_count);
	  return false;
	}
      if (!ims_arbitrate_loop (region, schedule, desc, body_count,
			       preheader_count, run_begins.length (), dump))
	{
	  rvtt_refuse (RVTT_REF_REPLAY_DELIVERY_PREFERRED, dump,
		       "Macro-planner formation-refusal:"
		       " replay-delivery-preferred\n");
	  return false;
	}
    }
  else
    for (unsigned b = 0; b != run_begins.length (); ++b)
      {
	unsigned begin = run_begins[b];
	unsigned end = b + 1 == run_begins.length ()
	  ? region.rows.length () : run_begins[b + 1];
	if (!run_profitable_p (region, schedule, desc, end - begin,
			       ih_pricing_stage,
			       init_prog.caller_entry_count,
			       init_prog.caller_body_count, dump))
	  {
	    if (dump)
	      fprintf (dump, "Macro-planner formation-refusal:"
		       " unprofitable\n");
	    return false;
	  }
	if (!ims_arbitrate_run (region, schedule, desc, end - begin,
				ih_pricing_stage,
				init_prog.caller_entry_count,
				init_prog.caller_body_count, dump))
	  {
	    rvtt_refuse (RVTT_REF_REPLAY_DELIVERY_PREFERRED, dump,
			 "Macro-planner formation-refusal:"
			 " replay-delivery-preferred\n");
	    return false;
	  }
      }

  /* Emission deletes each row's typed Dst separator; that is only
     sound when the launch calendar absorbed the stride, or when the
     proven program keeps the separator in place (WP9: the CC-template
     programs re-emit it verbatim as the restore-visibility slot).  */
  for (const macro_row &row : region.rows)
    if (row.separator && !schedule.absorbed_stride && !desc.keep_separator)
      {
	rvtt_refuse (RVTT_REF_STRIDE_NOT_ABSORBED, dump,
		     "Macro-planner formation-refusal:"
		     " stride-not-absorbed\n");
	return false;
      }

  /* Immediate-delta regions (lane IS, F1 honest fix): only the
     absorbed calendar expresses them (the schedule already refuses
     imm-stride-not-absorbed; this re-check keeps the contract locally
     auditable), the WP9 kept-separator program never carries them, and
     every Dst access of every offset row must be address-rewritable
     back to rows[0]'s base -- proven here as a dry run BEFORE any
     mutation.  */
  if (region.imm_stride)
    {
      if (schedule.absorbed_stride != region.imm_stride
	  || desc.keep_separator)
	{
	  rvtt_refuse (RVTT_REF_IMM_STRIDE_NOT_ABSORBED, dump,
		       "Macro-planner formation-refusal:"
		       " imm-stride-not-absorbed\n");
	  return false;
	}
      for (const macro_row &row : region.rows)
	{
	  if (!row.imm_delta)
	    continue;
	  for (rtx_insn *insn : row.insns)
	    {
	      xtt_effect_set e = rvtt_insn_effects (insn);
	      if (!e.dst_mem_read && !e.dst_mem_write)
		continue;
	      rtx address, mode, addr_mode;
	      if (!rvtt_dst_access_operands (insn, e, &address, &mode,
					     &addr_mode)
		  || !CONST_INT_P (address)
		  || !planner_rewrite_dst_address (insn, PATTERN (insn),
						   INTVAL (address)))
		{
		  rvtt_refuse (RVTT_REF_IMM_STRIDE_REWRITE_UNPROVEN, dump,
			       "Macro-planner formation-refusal:"
			       " imm-stride-rewrite-unproven\n");
		  return false;
		}
	    }
	}
    }

  /* WP10 compact CC calendar: the absorbing explicit load's address
     mode operand must be rewritable (the one admitted load pattern);
     proven here as a dry run -- refusal paths never mutate.  */
  if (schedule.absorb_into_explicit)
    for (unsigned ix = 0; ix != region.rows[0].insns.length (); ++ix)
      if (schedule.events[ix].absorbs_stride
	  && !planner_rewrite_load_addr_mode
	       (region.rows[0].insns[ix],
		PATTERN (region.rows[0].insns[ix]),
		c->auto_increment_dst2_addr_mode))
	{
	  rvtt_refuse (RVTT_REF_STRIDE_NOT_ABSORBED, dump,
		       "Macro-planner formation-refusal:"
		       " stride-not-absorbed\n");
	  return false;
	}

  /* WP11: cross-tile prefix elision for formed CC calendars.  When the
     configuration preheader itself sits inside an enclosing issue loop
     (the tile loop) and the configuration-epoch proof shows no
     intervening owner of the planner's SFPCONFIG destinations across
     that loop, the descriptor words are hoisted to the enclosing
     loop's structural preheader and elided from every later trip; the
     ambient enable and the owned SETC16 program stay per trip.  The
     hoisted block needs its own copyable proven all-lanes enable (the
     lane-predicated LREG materialization must not run masked), so a
     region relying purely on an in-place trailing enable copies that
     proven word.  Every refusal keeps today's per-trip prefix
     byte-identically, under a stable name.  */
  /* WP13 residency de-duplication: RESIDENT_ELIDE was computed above
     (moved ahead of profitability by lane IU, decision-identical --
     the lookup is proof-only and profitability reads no residency
     state); a resident program makes the WP11 hoist moot.  */
  basic_block hoist_preheader = nullptr;
  edge hoist_edge = nullptr;
  rtx_insn *hoist_enable_src = nullptr;
  if (!resident_elide && desc.cc.active && config_preheader)
    {
      hoist_enable_src = enable_src
	? enable_src : preheader_trailing_enable (config_preheader);
      if (hoist_enable_src
	  && !cc_enable_all_lanes_proved_p (hoist_enable_src))
	hoist_enable_src = nullptr;
      if (hoist_enable_src)
	{
	  const char *epoch_refusal = nullptr;
	  rtx_insn *epoch_refusal_insn = nullptr;
	  if (rvtt_macro_prefix_epoch_hoist (fn, region, config_preheader,
					     c, &hoist_preheader,
					     &hoist_edge, &epoch_refusal,
					     &epoch_refusal_insn))
	    {
	      /* WP13 residency outward extension (rvtt-macro-desc.cc):
		 proof-only iteration of the epoch discipline through
		 further enclosing loops; on any refusal the placement
		 stays WP11's, byte-identically.  */
	      unsigned resid_levels = 0;
	      rvtt_macro_residency_extend (fn, region, desc, c, resid,
					   &hoist_preheader, &hoist_edge,
					   &resid_levels, dump);

	      /* Commit-time edge split (guarded enclosing loop): every
		 proof has passed, so this is no longer a refusal path.
		 The split block executes exactly when the loop is
		 entered.  */
	      if (!hoist_preheader)
		hoist_preheader = split_edge (hoist_edge);
	      if (dump)
		{
		  unsigned words = 0;
		  for (unsigned t = 0; t != desc.n_templates; ++t)
		    words += config_word_loadi_issues (desc.templ[t]) + 1;
		  for (unsigned m = 0; m != desc.n_seq; ++m)
		    words += config_word_loadi_issues (desc.seq[m]) + 1;
		  if (desc.has_misc)
		    words += config_word_loadi_issues (desc.misc) + 1;
		  fprintf (dump, "Macro-planner prefix-epoch: cross-tile"
			   " config invariance proven (owned SFPCONFIG"
			   " dests epoch-clean across the enclosing loop;"
			   " %u descriptor words hoisted to the outer"
			   " preheader; enable+setc16 retained per"
			   " trip)\n", words);
		}
	    }
	  else if (epoch_refusal && dump)
	    {
	      rvtt_refuse_by_name (epoch_refusal, dump,
				   "Macro-planner prefix-epoch-refusal: %s",
				   epoch_refusal);
	      if (epoch_refusal_insn)
		fprintf (dump, " (insn %d)", INSN_UID (epoch_refusal_insn));
	      fprintf (dump, "\n");
	    }
	}
    }

  /* Lane CA cross-call init hoist (D2): when this straight-line region
     is the function's whole macro content and its idempotent init
     prefix is call-invariant descriptor data, prove the (single)
     caller's loop epoch and move the prefix to the caller's loop
     preheader -- once per loop instead of once per call.  The proof
     chain ran PROOF-ONLY ahead of profitability (lane IU pricing
     pre-run); the COMMIT stays here, LAST among the refusal points (a
     committed caller-side insertion and the callee-side suppression
     stand together).  The committing call re-evaluates the identical
     deterministic proof chain; a divergence would mean the function
     changed between the two calls -- impossible on the refusal-free
     path -- and refuses fail-closed (pricing that assumed the hoist
     must never form without it).  Every unproven link keeps today's
     per-call prefix byte-identically.  */
  if (init_hoist_stage > 0
      && !hoist_preheader && !config_preheader && !resident_elide)
    {
      const char *commit_refusal
	= rvtt_crosscall_init_hoist (fn, &init_prog, /*commit=*/true);
      if (commit_refusal || init_prog.stage != init_hoist_stage)
	{
	  rvtt_refuse (RVTT_REF_INIT_HOIST_COMMIT_DIVERGED, dump,
		       "Macro-planner formation-refusal:"
		       " init-hoist-commit-diverged (%s)\n",
		       commit_refusal ? commit_refusal : "stage-mismatch");
	  return false;
	}
      if (dump)
	fprintf (dump, "Macro-planner init-hoist: stage=%d"
		 " init contract hoisted to caller loop preheader"
		 " (%u descriptor words, %u setc16, enable %s)\n",
		 init_hoist_stage, init_prog.n_words, init_prog.n_setc16,
		 init_hoist_stage >= 2 ? "hoisted" : "retained");
    }
  else
    init_hoist_stage = 0;

  /* Drain-aware boundary placement (default-off; proofs and derivation
     in rtl-rvtt-schedule.cc): decide every intra-region boundary BEFORE any
     mutation.  The final run's drain -- the region's exit contract (no
     events in flight may reach the invisible follower stream) -- is
     never elided.  */
  /* Inter-row drain (lane EV, P0 wrong-code adjudication 2026-08-21).
     Within a run, rows are emitted back-to-back.  The conservative VD
     policy's own stated rule (rvtt-macro-sched.cc: a hosted launched
     event consumes the launch VD; without a proven consumption slot
     before the next row's launch, consecutive rows must alternate VDs)
     makes that sound ONLY under VD alternation.  When descriptor
     synthesis pins a VALUE carrier's VD (WP12 name-encoded consumers;
     the frozen whole-word programs' fixed_vd), every row re-targets the
     SAME register while the previous row's hosted consumers still pend
     up to drain_slots past its launch -- back-to-back rows race the
     next launch's VD write against the pending events.  Adjudicated on
     the replay-loop-unroll signbit shape (device corr FAIL, weekly
     pin-15 + pin-18 e2e; pinned-sim 32489dda reproduction; sim trace
     shows three launches' events in flight on one LReg and the shift
     event consuming overwritten data).  Placement: the FULL derived
     drain between consecutive rows, reproducing exactly the proven
     rolled per-row calendar (launch + drain), whose issue-stream
     spacing is the proven envelope.  The established alternating
     envelope, store-only sacrificial VDs (written, never read), and
     the CC-template model (its macro_cc_model next-row obligations --
     store-before-next-def, restore-visibility <= row interval -- are
     the proven inter-row contract, silicon-proven multi-row on the
     unified where kernel) keep today's bytes.  */
  bool interrow_drain = false;
  if (desc.drain_slots > 0 && region.rows.length () > 1 && !desc.cc.active)
    for (const macro_launch_spec &l : desc.launches)
      if (!l.vd_alternates && !l.is_store_only)
	interrow_drain = true;
  if (interrow_drain && dump)
    fprintf (dump, "Macro-planner drain-interrow: drain=%d rows=%u"
	     " (fixed-vd value carrier)\n", desc.drain_slots,
	     region.rows.length ());
  /* Lane FT window-pairing: replace the shape rule's full inter-row
     drain with the minimal spacing the exact pending-event model proves
     (rvtt_macro_interrow_drain_tuned, rtl-rvtt-schedule.cc).  Refusals
     keep the lane-EV placement byte-identically.  */
  int interrow_drain_slots = interrow_drain ? desc.drain_slots : 0;
  if (interrow_drain && riscv_tt_opt_window_pairing)
    {
      const char *bound = nullptr;
      int tuned = rvtt_macro_interrow_drain_tuned (fn, region, schedule,
						   desc, dump, &bound);
      gcc_assert (tuned >= 0 && tuned <= desc.drain_slots);
      if (tuned < interrow_drain_slots && dump)
	fprintf (dump, "Macro-planner window-pairing: interrow-drain"
		 " %d -> %d rows=%u%s%s\n", desc.drain_slots, tuned,
		 region.rows.length (), bound ? " bound=" : "",
		 bound ? bound : "");
      interrow_drain_slots = tuned;
    }

  auto_vec<bool> drain_elide;
  drain_elide.safe_grow_cleared (run_begins.length ());
  unsigned drains_elided = 0;
  if (riscv_tt_opt_drain_schedule && desc.drain_slots > 0)
    for (unsigned b = 0; b + 1 < run_begins.length (); ++b)
      {
	unsigned rend = run_begins[b + 1];
	unsigned rnext_end = b + 2 < run_begins.length ()
	  ? run_begins[b + 2] : region.rows.length ();
	drain_elide[b] = rvtt_macro_drain_boundary_elidable
	  (region, schedule, desc, run_begins[b], rend, rnext_end, dump);
	if (drain_elide[b])
	  ++drains_elided;
      }

  /* Loop-backedge drain elision (lane CA): a loop-body region's FINAL
     run ends at the loop latch, so its drain executes once per trip
     where the architecture requires it once per loop exit.  When the
     backedge follower stream proves (rvtt_macro_drain_backedge_elidable,
     rtl-rvtt-schedule.cc) AND a sound exit placement exists (the sole
     non-self successor, entered only from this loop), the in-body drain
     is elided and the FULL derived drain is emitted at the exit block's
     head instead -- the exit contract is preserved, only its placement
     moves.  Any unprovable piece keeps today's in-body drain
     byte-identically, under a stable name.  */
  bool backedge_elide = false;
  edge drain_exit_edge = nullptr;
  if (riscv_tt_opt_drain_schedule && desc.drain_slots > 0
      && region.loop_body && region.bb)
    {
      edge exit_e = nullptr;
      bool shape_ok = EDGE_COUNT (region.bb->succs) == 2;
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, region.bb->succs)
	if (e->dest != region.bb)
	  {
	    if (exit_e)
	      shape_ok = false;
	    exit_e = e;
	  }
      if (!shape_ok || !exit_e
	  || (exit_e->flags & (EDGE_ABNORMAL | EDGE_EH | EDGE_COMPLEX)))
	{
	  rvtt_refuse (RVTT_REF_DRAIN_EXIT_SHARED, dump,
		       "Macro-planner drain-refusal:"
		       " drain-exit-shared\n");
	}
      else
	{
	  unsigned first_run_end = run_begins.length () > 1
	    ? run_begins[1] : region.rows.length ();
	  backedge_elide = rvtt_macro_drain_backedge_elidable
	    (region, schedule, desc, first_run_end, dump);
	  if (backedge_elide)
	    drain_exit_edge = exit_e;
	}
    }

  basic_block config_placement = nullptr;
  for (unsigned b = 0; b != run_begins.length (); ++b)
    {
      unsigned begin = run_begins[b];
      unsigned end = b + 1 == run_begins.length ()
	? region.rows.length () : run_begins[b + 1];
      bool last_run = b + 1 == run_begins.length ();
      emit_planner_run (region, schedule, desc, c, begin, end, b == 0,
			config_preheader, enable_src,
			hoist_preheader, hoist_enable_src,
			last_run ? !backedge_elide : !drain_elide[b],
			interrow_drain_slots,
			resident_elide, resid, init_hoist_stage,
			b == 0 ? &config_placement : nullptr);
    }
  if (backedge_elide)
    {
      /* Exit compensation: the full derived drain on the loop's exit
	 path -- events from the final trip never reach the invisible
	 follower stream.  Placement: the exit destination's head when
	 this loop is its only predecessor, else a commit-time edge
	 split (every proof has passed; the split block executes
	 exactly when the loop exits) -- the same discipline as the
	 WP11 hoist's guarded-enclosing-loop split.  */
      basic_block dest = drain_exit_edge->dest;
      if (dest == EXIT_BLOCK_PTR_FOR_FN (fn) || !single_pred_p (dest)
	  || !bb_note (dest))
	dest = split_edge (drain_exit_edge);
      start_sequence ();
      for (int d = 0; d != desc.drain_slots; ++d)
	emit_insn (gen_rvtt_sfpnop ());
      rtx_insn *nops = get_insns ();
      end_sequence ();
      if (resid)
	for (rtx_insn *i = nops; i; i = NEXT_INSN (i))
	  resid->emitted.add (i);
      emit_insn_after (nops, bb_note (dest));
      if (dump)
	fprintf (dump, "Macro-planner drain-backedge: loop-carried drain"
		 " elided; exit compensation %d SFPNOPs (bb %d)\n",
		 desc.drain_slots, dest->index);
    }
  if (!resident_elide)
    rvtt_macro_residency_record (desc, config_placement, resid);
  if (dump)
    fprintf (dump, "Macro-planner formed: rows=%u runs=%u%s%s%s%s%s%s%s\n",
	     region.rows.length (), run_begins.length (),
	     config_preheader ? " config=preheader" : "",
	     materialized_enable ? " lane-proof=materialized-enable" : "",
	     hoist_preheader ? " prefix-epoch=hoisted" : "",
	     drains_elided ? " drain-elided" : "",
	     backedge_elide ? " drain-backedge" : "",
	     resident_elide ? " resident=elided" : "",
	     init_hoist_stage == 2 ? " init-hoist=full"
	     : init_hoist_stage == 1 ? " init-hoist=descriptor" : "");
  return true;
}

/* Drive one region through the established candidate search exactly as
   the pass spine always has: schedule candidates ascend, the first whose
   descriptor synthesis (and Layer-7 verification) proves is committed
   through form_region.  Returns true when a candidate PROVED (the search
   stops there whether or not formation committed); *CHANGED accumulates
   actual code mutation.  Shared verbatim by the spine and the WP15
   upward-carrier commit path so both can never diverge.  */

static bool
planner_process_region (function *fn, macro_region &region,
			macro_residency_state *resid, bool sole_region,
			FILE *dump, bool *changed)
{
  for (unsigned candidate = 0; ; ++candidate)
    {
      macro_schedule schedule;
      if (!rvtt_macro_schedule_region (region, &schedule, dump, candidate))
	return false;		/* search exhausted (or no table)  */
      bool proven = false;
      macro_descriptor descriptor;
      if (rvtt_macro_synthesize (region, schedule, &descriptor, dump))
	{
	  /* A Layer-7 verification mismatch is a descriptor refusal: the
	     candidate is unproven and must never reach form_region.  */
	  const char *verify_fail = nullptr;
	  if (riscv_tt_macro_planner_verify || flag_checking)
	    verify_fail = rvtt_macro_verify_descriptor (region, schedule,
							descriptor, dump);
	  proven = !descriptor.refusal && !verify_fail;
	  if (proven && riscv_tt_macro_planner)
	    *changed |= form_region (fn, region, schedule, descriptor,
				     resid, sole_region, dump);
	  rvtt_macro_descriptor_release (&descriptor);
	}
      rvtt_macro_schedule_release (&schedule);
      if (proven)
	return true;
    }
}

/* ---------------- WP15: upward-IMS carrier former ------------------- */
/* The upward half of the WP14 IMS mapping (-mtt-tensix-macro-ims-carrier,
   default off).  WP14's repair driver searches DOWNWARD -- reduced
   hosted sets -- and provably conserves the initiation interval on rows
   whose maximal hosting already proves (docs/MACRO_PLANNER.md 2d).  The
   upward former searches the other direction, the handwritten kernels'
   re-load idiom: duplicate one of the row's Dst loads into a provably
   free LREG (a fresh value carrier), replicate the load's in-place
   cooking prefix onto it, and version-split-rename one explicit consumer
   web onto the new carrier so its events can host there.  Everything is
   applied as a REAL commit-or-revert mutation of every unrolled row
   copy: the mutated region re-runs the full established pipeline --
   discovery, scheduling (including WP14 repair when enabled), descriptor
   synthesis, Layer-7 verification, and every formation gate -- which
   remains the only feasibility oracle.  A variant commits only when it
   re-proves at a STRICTLY smaller initiation interval than the
   established outcome; every other path reverts byte-identically under a
   stable refusal name.  Nothing here names an operation, matches an
   opcode calendar, or assembles a raw word: seeds, prefixes, and webs
   are typed-effect dataflow classes, and the duplication legality rides
   the effect vocabulary's own proofs (a region-admitted load is
   RWC-inert by the no-increment address-mode derivation).  */

/* Enumeration budget, NOT a cost-model constant: bounds the variant
   SEARCH per region (refusal-biased -- exhausting it leaves candidates
   unformed, never admits an unproven one).  No rvtt-cost.md derivation
   exists (FH audit FHP-5); widening or deriving it is the planner
   lane's follow-up.  */
static const unsigned UPWARD_CARRIER_BUDGET = 24; /* variants per region */

/* Stable refusal vocabulary (append-only dump API).  */
static const char *upward_refusal_legality
  = "ims-carrier-legality-unproven";
static const char *upward_refusal_lreg
  = "ims-carrier-lreg-unavailable";
static const char *upward_refusal_web
  = "ims-carrier-web-unsplittable";
static const char *upward_refusal_row_divergent
  = "ims-carrier-row-divergent";
static const char *upward_refusal_rederive
  = "ims-carrier-rederive-unproven";
static const char *upward_refusal_no_improvement
  = "ims-carrier-no-improvement";

/* Commit-or-revert journal.  Renamed insns keep their ORIGINAL pattern
   rtx (the mutation installs a copy), so revert restores the exact
   pre-mutation objects.  */

struct upward_journal
{
  auto_vec<rtx_insn *> inserted;
  auto_vec<rtx_insn *> renamed_insns;
  auto_vec<rtx> renamed_old_pats;
  auto_vec<int> renamed_old_codes;

  void revert ()
  {
    for (unsigned i = inserted.length (); i-- > 0;)
      delete_insn (inserted[i]);
    for (unsigned i = renamed_insns.length (); i-- > 0;)
      {
	PATTERN (renamed_insns[i]) = renamed_old_pats[i];
	INSN_CODE (renamed_insns[i]) = renamed_old_codes[i];
	df_insn_rescan (renamed_insns[i]);
      }
    drop ();
  }

  void drop ()
  {
    inserted.truncate (0);
    renamed_insns.truncate (0);
    renamed_old_pats.truncate (0);
    renamed_old_codes.truncate (0);
  }
};

/* Replace, in place, every hard-LREG occurrence in *LOC whose lane
   register number is in LREG_MASK by NEWREG.  Per-insn replacement is
   whole-register: within one instruction either every occurrence of a
   register renames (an in-place chain member: the post-RA tie holds
   because both sides move together) or the register only appears as
   reads (a stop-through consumer) -- the rename computation below only
   emits masks with that property.  */

static void
upward_replace_lregs (rtx *loc, uint32_t lreg_mask, rtx newreg)
{
  subrtx_ptr_iterator::array_type array;
  FOR_EACH_SUBRTX_PTR (iter, array, loc, NONCONST)
    {
      rtx *p = *iter;
      rtx x = *p;
      if (x && REG_P (x) && REGNO (x) >= SFPU_REG_FIRST
	  && REGNO (x) - SFPU_REG_FIRST < 32
	  && ((lreg_mask >> (REGNO (x) - SFPU_REG_FIRST)) & 1))
	*p = newreg;
    }
}

/* The carrier register choice.  The launch word's VDLo field encodes
   VD 0..3 only (VDHi is punned with the address LSB -- the capability
   tables' sacrificial-VD rule), so the carrier register must come from
   L0..L3.  When every low register is taken, a row-internal low WEB may
   be RELOCATED to a free high register first (the version-split-rename
   half of the pre-registered design): a pure whole-web physical rename
   is value-inert, and re-derivation re-proves every encoding that
   embedded the old name.  Highest-first choices keep away from L0,
   which additionally collides with the cast class's VC:=VD encoding
   (the derivation core's audited fact).  */

struct upward_carrier_choice
{
  int vd;			/* carrier register (0..3)	       */
  int relocate_to;		/* free high register, or -1	       */
};

static bool
upward_pick_carrier_reg (const macro_region &region,
			 upward_carrier_choice *out)
{
  basic_block bb = region.bb;
  uint32_t used = 0;
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    if (NONDEBUG_INSN_P (insn))
      {
	subrtx_iterator::array_type array;
	FOR_EACH_SUBRTX (iter, array, PATTERN (insn), NONCONST)
	  {
	    const_rtx x = *iter;
	    if (x && REG_P (x) && REGNO (x) >= SFPU_REG_FIRST
		&& REGNO (x) - SFPU_REG_FIRST < 32)
	      used |= 1u << (REGNO (x) - SFPU_REG_FIRST);
	  }
      }
  auto free_p = [&] (unsigned r) -> bool
    {
      return !((used >> r) & 1)
	&& !bitmap_bit_p (df_get_live_in (bb), SFPU_REG_FIRST + r)
	&& !bitmap_bit_p (df_get_live_out (bb), SFPU_REG_FIRST + r);
    };
  out->relocate_to = -1;
  for (unsigned r = 4; r-- > 0;)
    if (free_p (r))
      {
	out->vd = (int) r;
	return true;
      }
  /* Relocation: a free high register and a low register whose block
     usage is entirely this region's row members (row-internal web:
     defined and consumed inside the rows, never live across the block
     or referenced by foreign instructions).  */
  int high = -1;
  for (unsigned r = 8; r-- > 4;)
    if (free_p (r))
      {
	high = (int) r;
	break;
      }
  if (high < 0)
    return false;
  for (unsigned r = 4; r-- > 0;)
    {
      if (!((region.internal_lregs >> r) & 1)
	  || bitmap_bit_p (df_get_live_in (bb), SFPU_REG_FIRST + r)
	  || bitmap_bit_p (df_get_live_out (bb), SFPU_REG_FIRST + r))
	continue;
      bool foreign = false;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn) || foreign)
	    continue;
	  bool refs = false;
	  subrtx_iterator::array_type array;
	  FOR_EACH_SUBRTX (iter, array, PATTERN (insn), NONCONST)
	    {
	      const_rtx x = *iter;
	      if (x && REG_P (x)
		  && REGNO (x) == SFPU_REG_FIRST + r)
		refs = true;
	    }
	  if (!refs)
	    continue;
	  bool member = false;
	  for (const macro_row &row : region.rows)
	    for (rtx_insn *m : row.insns)
	      member |= m == insn;
	  foreign |= !member;
	}
      if (!foreign)
	{
	  out->vd = (int) r;
	  out->relocate_to = high;
	  return true;
	}
    }
  return false;
}

/* One enumerated variant: re-load the SEED load, replicate its in-place
   cooking prefix, and version-split the TARGET event's web onto the new
   carrier.  Indices are row0 positions; per-row application recomputes
   the same structure from each row's own registers (isomorphism makes
   the index sets agree; any divergence refuses).  */

struct upward_variant
{
  unsigned seed_ix;
  /* The version-split target CHAIN, program order: the first member
     reads the seed's cooked value; each later member reads the previous
     member's result.  Every member's fresh single-register definition
     web renames onto the new carrier -- the hand kernels' chained
     re-load idiom (a one-member chain is the plain split).  */
  unsigned targets[4];
  unsigned n_targets;
  /* Carrier placement: the re-load sits directly after the seed load,
     or -- when the cooking prefix is empty, so the copy has no in-row
     value dependences -- at the row head (an earlier launch slot gives
     the hosted chain earlier execution windows against its explicit
     consumers' deadlines).  Both positions read the same Dst word
     under the same counter state (the row's members are RWC-inert by
     admission).  */
  bool reload_at_head;
};

/* Derive the per-insn rename masks and the prefix-clone index set of one
   ROW for VARIANT.  Pure analysis (never mutates).  On success MASKS[i]
   holds the lane-register numbers to rewrite onto the new carrier in
   row insn i, and PREFIX holds the in-place cooking events to clone (in
   program order).  Refusals set *REFUSAL to a stable name.

   Soundness of the split, at the value level:
   - the seed is a region-admitted Dst load, so its address-mode operand
     is the derived no-increment slot (rvtt_insn_effects maps any other
     mode to an UNKNOWN RWC effect, which discovery refuses): executing
     the copy is architecturally inert beyond writing the new register;
   - the copy is inserted directly after the seed with the cooking
     clones following, and no member between the seed and a cloned
     event's original defines the clone's other sources, so every clone
     computes exactly the seed web's value into the new register;
   - the chain's first member reads the seed's cooked value (the new
     register holds that exact value after the clones) and each later
     member reads the previous member's result; every member writes a
     fresh single register, so renaming the member definitions onto the
     new carrier is a linear version split: the new register holds each
     chain value in turn, every use reached by a renamed definition is
     renamed with it, and a use of an earlier version positioned at or
     after the next version's definition refuses (the versions are only
     linear when their live ranges are).  After the chain tail the
     established single-web propagation continues: a tied in-place
     follower moves with the register (the post-RA tie holds because
     every occurrence renames together), a fresh redefinition ends the
     renamed range.  */

static bool
upward_compute_renames (const macro_row &row, const upward_variant &v,
			auto_vec<uint32_t> *masks,
			auto_vec<unsigned> *prefix, const char **refusal)
{
  unsigned n = row.insns.length ();
  masks->truncate (0);
  masks->safe_grow_cleared (n);
  prefix->truncate (0);

  if (v.n_targets == 0 || v.n_targets > 4)
    {
      *refusal = upward_refusal_legality;
      return false;
    }

  /* Seed legality: a plain (no live-value merge) Dst load writing one
     physical L0..L7 register.  The RWC-inert property is already the
     admission condition (see above).  */
  xtt_effect_set se = rvtt_insn_effects (row.insns[v.seed_ix]);
  if (se.opaque || !se.dst_mem_read
      || se.rwc.kind != xtt_rwc_effect_t::NONE)
    {
      *refusal = upward_refusal_legality;
      return false;
    }
  if (recog_memoized (row.insns[v.seed_ix]) != CODE_FOR_rvtt_sfpload_lv_int)
    {
      *refusal = upward_refusal_legality;
      return false;
    }
  extract_insn (row.insns[v.seed_ix]);
  if (recog_data.n_operands < 9
      || !noval_operand (recog_data.operand[6],
			 GET_MODE (recog_data.operand[6])))
    {
      /* A live-value merging load reads its destination's prior value;
	 duplicating it is not value-inert.  */
      *refusal = upward_refusal_legality;
      return false;
    }
  uint32_t dmask = se.lreg_write;
  if (!dmask || (dmask & (dmask - 1)) != 0 || (unsigned) ctz_hwi (dmask) > 7)
    {
      *refusal = upward_refusal_legality;
      return false;
    }

  /* Chain legality: ascending positions after the seed; the first
     member reads the seed register; each later member reads the
     previous member's result; every member is a non-memory value event
     writing one fresh (not self-read) register.  */
  uint32_t chain_w[4];
  for (unsigned k = 0; k != v.n_targets; ++k)
    {
      unsigned tix = v.targets[k];
      if (tix >= n || tix <= v.seed_ix
	  || (k && tix <= v.targets[k - 1]))
	{
	  *refusal = upward_refusal_legality;
	  return false;
	}
      xtt_effect_set te = rvtt_insn_effects (row.insns[tix]);
      uint32_t need = k == 0 ? dmask : chain_w[k - 1];
      if (te.opaque || te.dst_mem_read || te.dst_mem_write
	  || !(te.lreg_read & need))
	{
	  *refusal = upward_refusal_legality;
	  return false;
	}
      /* Two admitted member forms: a FRESH single-register definition
	 (version split), or an IN-PLACE continuation (the member reads
	 and writes the incoming version register -- the launch-VD chain
	 idiom itself); anything else is outside the split vocabulary.  */
      uint32_t w = te.lreg_write;
      bool in_place = w == need && (te.lreg_read & w) != 0;
      if (!w || (w & (w - 1)) != 0 || (!in_place && (te.lreg_read & w)))
	{
	  *refusal = upward_refusal_legality;
	  return false;
	}
      /* A later chain member must not read the SEED register: the new
	 carrier register no longer holds that value at its position.
	 (Reading it by its own name stays untouched and correct; only
	 a rename would be wrong, so nothing to rename means nothing to
	 refuse -- the mask below simply never adds dmask for k > 0.)  */
      chain_w[k] = w;
    }

  /* Cooking prefix: every writer of the seed register between the seed
     and the FIRST chain member must be an in-place event (reads and
     writes exactly that register); each is cloned onto the new
     carrier.  A fresh redefinition means the chain head does not
     consume the seed load's value at all.  */
  for (unsigned ix = v.seed_ix + 1; ix < v.targets[0]; ++ix)
    {
      xtt_effect_set e = rvtt_insn_effects (row.insns[ix]);
      if (e.opaque)
	continue;
      if (e.lreg_write & dmask)
	{
	  if (e.lreg_write != dmask || !(e.lreg_read & dmask)
	      || e.dst_mem_read || e.dst_mem_write)
	    {
	      *refusal = upward_refusal_legality;
	      return false;
	    }
	  /* The clone sits directly after the re-load; its other
	     sources must still carry their original reaching values
	     there: no member between the seed and this event may
	     define them.  */
	  uint32_t other = e.lreg_read & ~dmask;
	  for (unsigned jx = v.seed_ix + 1; jx < ix; ++jx)
	    if (rvtt_insn_effects (row.insns[jx]).lreg_write & other)
	      {
		*refusal = upward_refusal_web;
		return false;
	      }
	  prefix->safe_push (ix);
	}
    }

  /* Chain member renames: the head's reads of the seed register plus
     its definition; every later member's read of the previous version
     plus its definition.  */
  (*masks)[v.targets[0]] = dmask | chain_w[0];
  for (unsigned k = 1; k != v.n_targets; ++k)
    (*masks)[v.targets[k]] = chain_w[k - 1] | chain_w[k];

  /* Version linearity between chain members: a use of version k
     between its definition and the next chain member reads the new
     register (renamed); a use at or after the next member's definition
     would read a later version and refuses; an interleaved foreign
     redefinition of the version register likewise refuses.  */
  for (unsigned k = 0; k + 1 < v.n_targets; ++k)
    {
      uint32_t w = chain_w[k];
      unsigned from = v.targets[k] + 1;
      unsigned upto = v.targets[k + 1];
      for (unsigned ix = from; ix < upto; ++ix)
	{
	  xtt_effect_set e = rvtt_insn_effects (row.insns[ix]);
	  if (e.opaque)
	    {
	      *refusal = upward_refusal_web;
	      return false;
	    }
	  if (e.lreg_write & w)
	    {
	      /* A redefinition of the version register before the next
		 chain member: the split is not linear here.  */
	      *refusal = upward_refusal_web;
	      return false;
	    }
	  if (e.lreg_read & w)
	    (*masks)[ix] |= w;
	}
      /* Uses of a non-tail version after the next member's definition
	 read a later version: refuse.  (Reads of the OLD register name
	 past this window belong to other, unrenamed definitions only
	 when a fresh redefinition intervenes; without one such a read
	 consumed our renamed value and refuses.)  An in-place next
	 member carries the same register forward -- later reads bind to
	 the renamed continuation and its own windows judge them.  */
      if (chain_w[k + 1] == w)
	continue;
      for (unsigned ix = upto + 1; ix < n; ++ix)
	{
	  xtt_effect_set e = rvtt_insn_effects (row.insns[ix]);
	  if (e.opaque)
	    continue;
	  if ((e.lreg_write & w) && !(e.lreg_read & w))
	    break;		/* fresh redefinition: later reads foreign */
	  if (e.lreg_read & w)
	    {
	      *refusal = upward_refusal_web;
	      return false;
	    }
	}
    }

  /* Established single-web propagation past the chain tail.  */
  uint32_t wmask = chain_w[v.n_targets - 1];
  bool active = true;
  for (unsigned ix = v.targets[v.n_targets - 1] + 1; ix < n && active; ++ix)
    {
      xtt_effect_set e = rvtt_insn_effects (row.insns[ix]);
      if (e.opaque)
	{
	  /* An opaque member's register accesses are unknown; a live
	     renamed value across it cannot be proven to move.  */
	  *refusal = upward_refusal_web;
	  return false;
	}
      bool reads = (e.lreg_read & wmask) != 0;
      bool writes = (e.lreg_write & wmask) != 0;
      if (reads)
	{
	  (*masks)[ix] |= wmask;
	  if (writes && e.lreg_write != wmask)
	    {
	      /* Writes the renamed register AND another: outside the
		 single-result web vocabulary this split can prove.  */
	      *refusal = upward_refusal_web;
	      return false;
	    }
	}
      else if (writes)
	/* Fresh redefinition: later uses read it, unrenamed.  */
	active = false;
    }
  return true;
}

/* Apply VARIANT to every row of REGION (journal-recorded).  NEWREG_LREG
   is the free carrier register.  Returns false -- after reverting any
   partial application -- with *REFUSAL named when a row diverges from
   the row0 structure.  */

static bool
upward_apply (macro_region &region, const upward_variant &v,
	      const upward_carrier_choice &regs,
	      const auto_vec<uint32_t> &masks0,
	      const auto_vec<unsigned> &prefix0, upward_journal *journal,
	      const char **refusal)
{
  int newreg_lreg = regs.vd;
  rtx newreg = gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST + newreg_lreg);
  /* Web relocation first: the freed low register becomes the carrier.
     A pure whole-web physical rename of a proven row-internal web; the
     re-derivation re-proves every encoding embedding the name.  */
  if (regs.relocate_to >= 0)
    {
      rtx high = gen_rtx_REG (XTT32SImode,
			      SFPU_REG_FIRST + regs.relocate_to);
      for (macro_row &row : region.rows)
	for (rtx_insn *insn : row.insns)
	  {
	    bool refs = false;
	    subrtx_iterator::array_type array;
	    FOR_EACH_SUBRTX (iter, array, PATTERN (insn), NONCONST)
	      {
		const_rtx x = *iter;
		if (x && REG_P (x)
		    && REGNO (x) == SFPU_REG_FIRST + (unsigned) regs.vd)
		  refs = true;
	      }
	    if (!refs)
	      continue;
	    rtx old_pat = PATTERN (insn);
	    int old_code = INSN_CODE (insn);
	    rtx new_pat = copy_rtx (old_pat);
	    upward_replace_lregs (&new_pat, 1u << regs.vd, high);
	    PATTERN (insn) = new_pat;
	    INSN_CODE (insn) = -1;
	    journal->renamed_insns.safe_push (insn);
	    journal->renamed_old_pats.safe_push (old_pat);
	    journal->renamed_old_codes.safe_push (old_code);
	    if (recog_memoized (insn) < 0)
	      {
		*refusal = upward_refusal_web;
		journal->revert ();
		return false;
	      }
	    df_insn_rescan (insn);
	  }
    }
  for (macro_row &row : region.rows)
    {
      auto_vec<uint32_t> masks;
      auto_vec<unsigned> prefix;
      if (row.insns.length () != masks0.length ()
	  || !upward_compute_renames (row, v, &masks, &prefix, refusal))
	{
	  if (*refusal == nullptr)
	    *refusal = upward_refusal_row_divergent;
	  journal->revert ();
	  return false;
	}
      /* The per-row structure must agree with row0's: same clone set
	 and same rename positions (isomorphism should force this; any
	 divergence refuses rather than trusts).  */
      bool agrees = prefix.length () == prefix0.length ();
      for (unsigned i = 0; agrees && i != prefix.length (); ++i)
	agrees = prefix[i] == prefix0[i];
      for (unsigned i = 0; agrees && i != masks.length (); ++i)
	agrees = (masks[i] != 0) == (masks0[i] != 0);
      if (!agrees)
	{
	  *refusal = upward_refusal_row_divergent;
	  journal->revert ();
	  return false;
	}

      /* Renames first (patterns swap to mutated copies).  */
      for (unsigned ix = 0; ix != row.insns.length (); ++ix)
	{
	  if (!masks[ix])
	    continue;
	  rtx_insn *insn = row.insns[ix];
	  rtx old_pat = PATTERN (insn);
	  int old_code = INSN_CODE (insn);
	  rtx new_pat = copy_rtx (old_pat);
	  upward_replace_lregs (&new_pat, masks[ix], newreg);
	  PATTERN (insn) = new_pat;
	  INSN_CODE (insn) = -1;
	  journal->renamed_insns.safe_push (insn);
	  journal->renamed_old_pats.safe_push (old_pat);
	  journal->renamed_old_codes.safe_push (old_code);
	  if (recog_memoized (insn) < 0)
	    {
	      *refusal = upward_refusal_web;
	      journal->revert ();
	      return false;
	    }
	  df_insn_rescan (insn);
	}

      /* The re-load and the cooking clones, directly after the seed
	 (or the re-load alone at the row head for a prefix-free
	 variant).  */
      uint32_t dmask
	= rvtt_insn_effects (row.insns[v.seed_ix]).lreg_write;
      rtx reload_pat = copy_rtx (PATTERN (row.insns[v.seed_ix]));
      upward_replace_lregs (&reload_pat, dmask, newreg);
      rtx_insn *reload;
      if (v.reload_at_head && prefix.is_empty ())
	reload = emit_insn_before (reload_pat, row.insns[0]);
      else
	reload = emit_insn_after (reload_pat, row.insns[v.seed_ix]);
      journal->inserted.safe_push (reload);
      rtx_insn *pos = reload;
      bool clones_ok = recog_memoized (reload) >= 0;
      if (clones_ok)
	df_insn_rescan (reload);
      for (unsigned i = 0; clones_ok && i != prefix.length (); ++i)
	{
	  rtx clone_pat = copy_rtx (PATTERN (row.insns[prefix[i]]));
	  upward_replace_lregs (&clone_pat, dmask, newreg);
	  rtx_insn *clone = emit_insn_after (clone_pat, pos);
	  journal->inserted.safe_push (clone);
	  pos = clone;
	  clones_ok = recog_memoized (clone) >= 0;
	  if (clones_ok)
	    df_insn_rescan (clone);
	}
      if (!clones_ok)
	{
	  *refusal = upward_refusal_legality;
	  journal->revert ();
	  return false;
	}
    }
  return true;
}

/* First proven candidate of REGION through the established pipeline
   (analysis only, no dumps, no mutation).  */

static bool
upward_probe_region (const macro_region &region, int *ii_out,
		     unsigned *candidate_out, FILE *dump = nullptr)
{
  for (unsigned candidate = 0; ; ++candidate)
    {
      macro_schedule schedule;
      if (!rvtt_macro_schedule_region (region, &schedule, dump,
				       candidate))
	return false;
      bool proven = false;
      macro_descriptor descriptor;
      if (rvtt_macro_synthesize (region, schedule, &descriptor, dump))
	{
	  const char *verify_fail = nullptr;
	  if (riscv_tt_macro_planner_verify || flag_checking)
	    verify_fail = rvtt_macro_verify_descriptor (region, schedule,
							descriptor, dump);
	  proven = !descriptor.refusal && !verify_fail;
	  rvtt_macro_descriptor_release (&descriptor);
	}
      int ii = schedule.ii;
      rvtt_macro_schedule_release (&schedule);
      if (proven)
	{
	  *ii_out = ii;
	  *candidate_out = candidate;
	  return true;
	}
    }
}

/* The WP15 driver: try upward-carrier variants on REGION; returns true
   when one committed (the mutated region formed).  On false the
   function is byte-identical to entry.  */

static bool
upward_carrier_try (function *fn, macro_region &region,
		    macro_residency_state *resid, bool sole_region,
		    FILE *dump, bool *changed)
{
  if (region.rows.is_empty ())
    return false;

  /* Predicate-definition rows keep the established candidate space:
     their hosting rules are the proven CC select programs' territory
     (the WP14 discipline).  */
  for (rtx_insn *insn : region.rows[0].insns)
    {
      xtt_effect_set e = rvtt_insn_effects (insn);
      if (!(e.dst_mem_read || e.dst_mem_write) && e.cc_write
	  && e.lreg_read != 0 && !e.lreg_write)
	return false;
    }

  /* The established outcome is the improvement baseline: the upward
     search only ever replaces a PROVEN formation by a strictly denser
     one.  When the established search proves NOTHING, the upward
     variants may still recover the region (the WP14 repair symmetry:
     the re-load can be exactly what makes a refusing hosted set
     realizable); the baseline is then no formation at all, and the
     established profitability and arbitration gates inside
     form_region price the variant against the explicit stream.  */
  int est_ii = INT_MAX;
  unsigned est_candidate = 0;
  bool est_proven = upward_probe_region (region, &est_ii, &est_candidate);

  upward_carrier_choice reg_choice;
  if (!upward_pick_carrier_reg (region, &reg_choice))
    {
      rvtt_refuse_by_name (upward_refusal_lreg, dump,
			   "Macro-planner upward-carrier-refusal: %s\n",
			   upward_refusal_lreg);
      return false;
    }
  int newreg = reg_choice.vd;

  /* Classify the established schedule's explicit value events (the
     hosting frontier the upward search can move).  For an unproven
     region the frontier comes from the first grouping proposal (the
     maximal-sharing candidate always exists for a discovered region).  */
  macro_schedule est;
  if (!rvtt_macro_schedule_region (region, &est, nullptr,
				   est_proven ? est_candidate : 0))
    return false;
  const macro_row &row0 = region.rows[0];
  auto_vec<upward_variant> variants;
  /* A hostable chain member: an explicit non-memory value event of a
     hostable sub-unit class writing one fresh single register.  */
  auto chain_member_p = [&] (unsigned ix, uint32_t need) -> bool
    {
      /* For a PROVEN baseline only explicit events are a frontier (a
	 hosted event already realizes); an unproven baseline's greedy
	 hosting is not a realization, so every value event is fair.  */
      if (est_proven
	  && est.events[ix].realization != macro_event::EXPLICIT_INSN)
	return false;
      if (!est_proven
	  && est.events[ix].realization == macro_event::CC_COALESCED)
	return false;
      xtt_effect_set te = rvtt_insn_effects (row0.insns[ix]);
      if (te.opaque || te.dst_mem_read || te.dst_mem_write
	  || !(te.lreg_read & need)
	  || (te.subunit != XTT_SU_SIMPLE && te.subunit != XTT_SU_ROUND
	      && te.subunit != XTT_SU_MAD))
	return false;
      uint32_t w = te.lreg_write;
      bool in_place = w == need && (te.lreg_read & w) != 0;
      return w && (w & (w - 1)) == 0
	&& (in_place || !(te.lreg_read & w));
    };
  for (unsigned seed_ix = 0; seed_ix != row0.insns.length (); ++seed_ix)
    {
      xtt_effect_set se = rvtt_insn_effects (row0.insns[seed_ix]);
      if (se.opaque || !se.dst_mem_read)
	continue;
      uint32_t dmask = se.lreg_write;
      if (!dmask || (dmask & (dmask - 1)) != 0)
	continue;
      for (unsigned tix = seed_ix + 1; tix != row0.insns.length (); ++tix)
	{
	  if (variants.length () >= UPWARD_CARRIER_BUDGET)
	    break;
	  if (!chain_member_p (tix, dmask))
	    continue;
	  /* Greedy deterministic chain: extend with the next explicit
	     event reading the current tail's result, as long as that
	     result has a UNIQUE reading successor position (linear
	     version ranges; upward_compute_renames re-proves).  */
	  upward_variant v;
	  v.seed_ix = seed_ix;
	  v.targets[0] = tix;
	  v.n_targets = 1;
	  v.reload_at_head = false;
	  while (v.n_targets < 4)
	    {
	      uint32_t w = rvtt_insn_effects
		(row0.insns[v.targets[v.n_targets - 1]]).lreg_write;
	      /* The tail version's live window ends at the next
		 definition of its register; the extension member is
		 that definition when it is an in-place continuation
		 (it reads the version), else the window's LAST reader
		 (earlier readers stay read-renamed uses of the same
		 version).  */
	      int next = -1;
	      for (unsigned jx = v.targets[v.n_targets - 1] + 1;
		   jx != row0.insns.length (); ++jx)
		{
		  xtt_effect_set je = rvtt_insn_effects (row0.insns[jx]);
		  if (je.opaque)
		    continue;
		  if (je.lreg_write & w)
		    {
		      if (je.lreg_read & w)
			next = (int) jx;   /* in-place continuation */
		      break;	/* any definition ends the window    */
		    }
		  if (je.lreg_read & w)
		    next = (int) jx;	   /* last reader so far      */
		}
	      if (next < 0 || !chain_member_p ((unsigned) next, w))
		break;
	      v.targets[v.n_targets++] = (unsigned) next;
	    }
	  /* Placement axis: a prefix-free variant (no cooking writer of
	     the seed register before the chain head) additionally tries
	     the row-head carrier slot FIRST -- the earlier launch slot
	     gives the hosted chain earlier execution windows against
	     its explicit consumers' deadlines.  */
	  bool prefix_free = true;
	  for (unsigned jx = seed_ix + 1; jx < tix; ++jx)
	    {
	      xtt_effect_set je = rvtt_insn_effects (row0.insns[jx]);
	      if (!je.opaque && (je.lreg_write & dmask))
		prefix_free = false;
	    }
	  /* The maximal chain first, then each shorter prefix (a deeper
	     rename can refuse where a shallower one proves).  */
	  for (unsigned len = v.n_targets; len > 0; --len)
	    {
	      if (variants.length () >= UPWARD_CARRIER_BUDGET)
		break;
	      upward_variant p = v;
	      p.n_targets = len;
	      if (prefix_free)
		{
		  p.reload_at_head = true;
		  variants.safe_push (p);
		  if (variants.length () >= UPWARD_CARRIER_BUDGET)
		    break;
		}
	      p.reload_at_head = false;
	      variants.safe_push (p);
	    }
	}
    }
  rvtt_macro_schedule_release (&est);

  for (const upward_variant &v : variants)
    {
      char chain_str[32];
      {
	int off = 0;
	for (unsigned k = 0; k != v.n_targets && off < 24; ++k)
	  off += snprintf (chain_str + off, sizeof (chain_str) - off,
			   "%s%u", k ? "," : "", v.targets[k]);
      }
      const char *refusal = nullptr;
      auto_vec<uint32_t> masks0;
      auto_vec<unsigned> prefix0;
      if (!upward_compute_renames (row0, v, &masks0, &prefix0, &refusal))
	{
	  rvtt_refuse_by_name (refusal, dump,
			       "Macro-planner upward-carrier-refusal: %s"
			       " (seed=%u chain={%s})\n", refusal, v.seed_ix,
			       chain_str);
	  continue;
	}
      upward_journal journal;
      if (!upward_apply (region, v, reg_choice, masks0, prefix0, &journal,
			 &refusal))
	{
	  rvtt_refuse_by_name (refusal, dump,
			       "Macro-planner upward-carrier-refusal: %s"
			       " (seed=%u chain={%s})\n", refusal, v.seed_ix,
			       chain_str);
	  continue;
	}

      /* Re-derive: the mutated block re-enters discovery; the variant's
	 region must reappear with the same row count (the whole
	 unrolled span re-proves or the variant refuses).  */
      auto_vec<macro_region> fresh;
      rvtt_macro_regions_discover (fn, nullptr, &fresh);
      macro_region *sel = nullptr;
      rtx_insn *row0_reload = journal.inserted[0];
      for (macro_region &fr : fresh)
	if (fr.bb == region.bb
	    && fr.rows.length () == region.rows.length () && !sel)
	  for (rtx_insn *i : fr.rows[0].insns)
	    if (i == row0_reload)
	      {
		sel = &fr;
		break;
	      }

      bool committed = false;
      int new_ii = 0;
      unsigned new_candidate = 0;
      /* The variant's re-derivation search is dumped in full (the WP14
	 repair discipline): the probe's schedule and descriptor lines
	 are the reviewable record of why a variant proves or dies.  */
      if (dump)
	{
	  fprintf (dump, "Macro-planner upward-carrier: probing seed=%u"
		   " chain={%s} reload-vd=%d placement=%s", v.seed_ix,
		   chain_str, newreg, v.reload_at_head ? "head" : "after-seed");
	  if (reg_choice.relocate_to >= 0)
	    fprintf (dump, " web-relocated=L%d->L%d", reg_choice.vd,
		     reg_choice.relocate_to);
	  fprintf (dump, "\n");
	}
      if (!sel || !upward_probe_region (*sel, &new_ii, &new_candidate, dump))
	{
	  rvtt_refuse_by_name (upward_refusal_rederive, dump,
			       "Macro-planner upward-carrier-refusal: %s"
			       " (seed=%u chain={%s})\n", upward_refusal_rederive,
			       v.seed_ix, chain_str);
	}
      else if (new_ii >= est_ii)
	{
	  rvtt_refuse_by_name (upward_refusal_no_improvement, dump,
			       "Macro-planner upward-carrier-refusal: %s"
			       " (seed=%u chain={%s} est-ii=%d variant-ii=%d)\n",
			       upward_refusal_no_improvement, v.seed_ix, chain_str,
			       est_ii, new_ii);
	}
      else
	{
	  if (dump)
	    fprintf (dump, "Macro-planner upward-carrier: seed=%u chain={%s}"
		     " reload-vd=%d prefix-clones=%u ii=%d->%d\n",
		     v.seed_ix, chain_str, newreg, prefix0.length (),
		     est_ii, new_ii);
	  bool local_changed = false;
	  bool proven = planner_process_region (fn, *sel, resid, sole_region,
						dump, &local_changed);
	  if (proven && local_changed)
	    {
	      *changed = true;
	      committed = true;
	      if (dump)
		fprintf (dump, "Macro-planner upward-carrier: formed"
			 " (ii=%d, was %d)\n", new_ii, est_ii);
	    }
	  else if (dump)
	    rvtt_refuse_by_name (upward_refusal_rederive, dump,
				 "Macro-planner upward-carrier-refusal: %s"
				 " (seed=%u chain={%s} formation declined)\n",
				 upward_refusal_rederive, v.seed_ix, chain_str);
	}

      for (macro_region &fr : fresh)
	rvtt_macro_region_release (&fr);
      if (committed)
	{
	  journal.drop ();	/* mutation is the committed code */
	  return true;
	}
      journal.revert ();
    }
  return false;
}

const pass_data pass_data_rvtt_macro_planner =
{
  RTL_PASS,
  "rvtt_macro_planner",
  OPTGROUP_OTHER,
  TV_NONE,
  0,
  0,
  0,
  0,
  0
};

class pass_rvtt_macro_planner : public rtl_opt_pass
{
public:
  pass_rvtt_macro_planner (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_macro_planner, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX
      && (riscv_tt_macro_planner_analyze || riscv_tt_macro_planner);
  }

  unsigned execute (function *fn) final override
  {
    bool changed = false;
    /* Planner emission records are per-function: clear the previous
       function's launch effect records before any formation
       (rvtt-effects.h contract; lookups additionally verify the
       function identity, so this reset is belt-and-braces).  */
    rvtt_planner_launch_effects_reset ();
    /* WP13 residency: per-function store of programmed descriptor
       content and planner-emitted insns (rvtt-macro-desc.cc).  Regions
       are processed in discovery order = forward program order, the
       increment-1 first-formed-wins selection policy.  */
    macro_residency_state resid;
    auto_vec<macro_region> regions;
    rvtt_macro_regions_discover (fn, dump_file, &regions);
    for (macro_region &region : regions)
      {
	/* WP15 upward-IMS carrier former (default off): when a variant
	   commits, the mutated region has already formed and the
	   established search is superseded for this region.  On any
	   refusal the function is byte-identical and the established
	   search below proceeds untouched.  */
	if (riscv_tt_macro_planner && riscv_tt_macro_ims_carrier
	    && upward_carrier_try (fn, region, &resid,
				   regions.length () == 1, dump_file,
				   &changed))
	  {
	    rvtt_macro_region_release (&region);
	    continue;
	  }
	/* Deterministic carrier-grouping search: candidates ascend from
	   maximal sharing; the first whose descriptor synthesis proves
	   is committed.  When every candidate refuses, the region
	   refuses byte-identically.  */
	planner_process_region (fn, region, &resid, regions.length () == 1,
				dump_file, &changed);
	rvtt_macro_region_release (&region);
      }
    return changed ? TODO_df_finish : 0;
  }
};

} // anonymous namespace

rtl_opt_pass *
make_pass_rvtt_macro_planner (gcc::context *ctxt)
{
  return new pass_rvtt_macro_planner (ctxt);
}
