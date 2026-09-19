/* Tensix SFPLOADMACRO planner: ownership proofs, cost model and
   emission primitives.

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

/* Split out of rtl-rvtt-macro-planner.cc; see
   rtl-rvtt-macro-planner-int.h for what crosses.  */

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
#include "rtl-rvtt-macro-planner-int.h"

namespace rvtt_planner {

/* ---------------- Formation: emission from the descriptor ------------ */

/* Function-global configuration-ownership proof, typed: the planner owns
   the macro configuration destinations and address-modifier slots for
   the whole function under the formation contract, so any call, any
   asm, and any typed config access touching an owned destination (or a
   statically unknown one) refuses.  Path-sensitive refinement through
   rvtt-macro-ownership is a documented later widening (fresh tests
   required); this matches the frozen contract byte for byte.  */

bool
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

bool
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

/* Region-scoped configuration ownership (the refinement the
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
     convention (carried from the frozen pass and its hardware-proven
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

/* The region-scoped ownership check itself (bounds above): every insn
   from SCOPE_BEGIN (straight-line: the prefix anchor) or from REGION's
   block head (loop body, CONFIG_PREHEADER non-null) through the region
   end must be clean -- no call, no unproven raw asm, no typed access to
   a configuration destination owned per C; for a loop-body region the
   preheader's tail insn (bar a trailing jump) is verified as well.  */

bool
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

bool
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
   loadi_issue_words; the one delivery-cost API).  */

unsigned
config_word_loadi_issues (uint32_t w)
{
  return rvtt_dcost_loadi_issue_words (w);
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

   Init-hoist-aware run pricing (rvtt-cost.md
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

bool
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
      if (dump)
	fprintf (dump, "Macro-planner run-pricing: init-hoist-amortized"
		 " config=%llu per-run=%llu explicit=%llu"
		 " weight=%lld/%lld -> %s\n",
		 (unsigned long long) config, (unsigned long long) per_run,
		 (unsigned long long) explicit_side, (long long) ih_body,
		 (long long) ih_entry, ok ? "profitable" : "unprofitable");
      return ok;
    }
  return rvtt_delivery_cost::run_amortized_p (config, per_run,
					      explicit_side, 1, 1);
}

/* Formation-vs-replay arbitration (-mtt-tensix-macro-ims).  The
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
  return formed;
}

/* Steady-state lower bound of the replay-delivered explicit
   alternative: every row instance re-executes its words at the slot
   rate with delivery hidden; record and launch words charged at
   zero (refusal-biased).  */

static uint64_t
ims_replay_alt_cost_x100 (const macro_region &region, unsigned run_rows)
{
  return rvtt_delivery_cost::ims_replay_alt_cost_x100
    (rvtt_dcost_table (), run_rows, ims_replayed_row_words (region));
}

/* Arbitration between forming RUN_ROWS rows of REGION as a macro run
   and leaving them to replay delivery: the formed calendar's centislot
   price (SCHEDULE, DESC) must beat the replay-delivered explicit
   alternative's steady-state lower bound.  IH_STAGE/IH_ENTRY/IH_BODY
   carry the crosscall init-hoist state: under a proven stage-2 contract
   both sides are weighed by the caller-loop profile fraction (the
   prefix by entry, the rest by body).  Returns true to form --
   vacuously when the IMS or replay flag is off or the rows are not
   replay-safe, so the arbitration only ever vetoes.  Prices and verdict
   go to DUMP.  */

bool
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
  /* Init-hoist-aware arbitration: under the proven stage-2
     contract the prefix's push words execute once per caller-loop
     entry; weigh both sides by the same profile fraction the run
     pricing uses (cross-multiplied, no rounding).  The replay
     alternative carries no prefix, so its side scales by body
     alone -- refusal-biased as before.  */
  if (ih_stage >= 2 && ih_body > 0 && ih_entry > 0 && ih_body >= ih_entry)
    {
      uint64_t prefix = (uint64_t) rvtt_dcost_words_to_centislots
	(config_prefix_cost (desc), rvtt_delivery_cost::PLANE_RISC_PUSH);
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

bool
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

/* Loop trip weight: the profile-estimated body/preheader
   execution-count ratio of a loop-body region.  Purely a profitability
   weight -- never a correctness input -- exact where the profile is
   (constant-bound loops), the static estimate elsewhere.  The two
   outputs report the ratio as an unreduced fraction so profitability
   can weigh it without rounding.  Returns false when the profile gives
   no usable estimate.  */

bool
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

bool
loop_profitable_p (const macro_region &region, const macro_schedule &schedule,
		   const macro_descriptor &desc, gcov_type body_count,
		   gcov_type preheader_count, unsigned n_runs)
{
  unsigned total_rows = region.rows.length ();
  unsigned per_trip_macro = total_rows * schedule.ii
    + n_runs * desc.drain_slots;
  unsigned per_trip_explicit = total_rows * explicit_row_cost (region);
  return rvtt_delivery_cost::run_amortized_p
    (config_prefix_cost (desc), per_trip_macro, per_trip_explicit,
     (uint64_t) preheader_count, (uint64_t) body_count);
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

bool
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

rtx_insn *
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

/* Entry-ambient all-lanes derivation (an owner-ratified honesty
   fix): whether the lane-enable state at the configuration
   placement point is provably the architectural all-lanes state, with
   NO marker instruction in the stream.  The ambient model is the
   established structured-CC lowering contract (gimple-rvtt-cc.cc: the
   outermost lane state is pinned all-lanes -- the same license behind
   the outermost POPC -> ENCC rewrite, the enable materialization, the
   crossrow-pairing loop-entry walk, and prgm-const's pre-peel walk):
   function entry is all-lanes ambient; a word-exact all-lanes SFPENCC
   KILLS (re-establishes the state); any other CC-affecting statement
   dirties.  Scalar (non-Tensix) instructions cannot touch SFPU lane
   state and are transparent.

   Audited-word walk transparency: the original
   walk was fail-closed on ALL asm -- the real LLK kernels inline their
   envelope init as raw `.ttinsn' TTI_ words, so every production
   preheader chain crossed "opaque" init and refused
   (ambient-entry-unproven).  The walk now DERIVES a verdict from the
   decoded content instead of refusing on shape (the record-window
   discipline: derive from decoded fields, never trust):

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

bool
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
   whole-body ownership obligations.  The loop header must have
   exactly its backedge plus one external incoming edge; the incoming
   block must have no other successor, which proves at least one trip on
   this edge, so hoisting the all-lanes enable is not a zero-trip CC
   change; and every Tensix issue in the body must belong to the region,
   so no foreign issue can mutate configuration, CC, or counter state
   between the preheader materialization and any launch.  Refusal paths
   return null after dumping a stable name.  */

basic_block
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
   pattern to ADDR_MODE (compact CC calendar: the trailing load's
   own auto-increment mode absorbs the deleted separator's stride).
   The operand position mirrors rvtt_dst_access_operands' positional
   knowledge for the one admitted load pattern: rvtt_sfpload_lv_int
   carries operands 1..8 as unspec_volatile vector elements 0..7, so
   the addr_mode operand 8 is element 7.  Returns false -- without
   mutating -- for any other pattern; formation checks this BEFORE any
   emission so refusal paths never mutate.  */

bool
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
   NEW_ADDR (honesty fix: immediate-delta rows normalize
   their explicit reloads back to rows[0]'s base -- the absorbed-stride
   calendar supplies the per-row advance through the counter).  The
   operand positions mirror rvtt_dst_access_operands' positional
   knowledge for the two admitted Dst patterns; the address element is
   unspec element 3 in both.  Returns false -- without mutating -- for
   any other shape; formation dry-runs this BEFORE any emission so
   refusal paths never mutate.  */

bool
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

/* Emit one formed run covering rows [BEGIN, END) of REGION under
   SCHEDULE and DESC.  With EMIT_CONFIG the configuration prefix
   (all-lanes enable copied from ENABLE_SRC, owned SETC16 program,
   descriptor words) is established first -- before the anchor, at
   CONFIG_PREHEADER's tail, or (WP11 cross-tile epoch) split between
   HOIST_PREHEADER using HOIST_ENABLE_SRC and a retained per-trip part
   -- reduced per the annotated INIT_HOIST_STAGE and RESIDENT_ELIDE
   modes; *CONFIG_PLACEMENT reports where descriptor words were newly
   programmed.  The rows' insns are then replaced by the scheduled
   calendar: launches (with recorded issue-plane effects), retargeted
   explicit reloads, kept separators, inter-row drains, and the run-end
   drain under EMIT_DRAIN.  Everything emitted is recorded in RESID as
   planner-emitted.  */

void
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
		     provenance), or the smaller residual
		     window-pairing tuning proved
		     (rvtt_macro_interrow_drain_tuned, under
		     -mtt-tensix-optimize-window-pairing).  */
		  int interrow_drain_slots,
		  /* Descriptor residency (rvtt-macro-desc.cc): elide the
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
	 enable, or the first row's own proven all-lanes restore
	 materialized in the prefix -- both proven word-exact all-lanes
	 by formation (cc_enable_all_lanes_proved_p), so this copy
	 re-establishes exactly the proven state; the first-row relaxation
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
	  /* Descriptor residency: the descriptor words are already resident
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
	  /* Cross-tile configuration epoch: the descriptor words
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
     for a load feeding the coalesced lane-merge -- the shared
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
		/* Compact CC calendar: the trailing explicit load
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
		/* Immediate-delta row (honesty fix):
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
      /* The proven CC-template program keeps the row's typed
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
     drain) is planner-emitted and benign for the residency walks
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

const char *
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

} /* namespace rvtt_planner */
