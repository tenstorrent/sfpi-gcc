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

/* Ownership checks, profitability and emission for the SFPLOADMACRO
   planner (rtl-rvtt-macro-planner.cc); the interface between the two
   files is rtl-rvtt-macro-planner-int.h.  */

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
#include "rvtt-rtl-lane-state.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-macro-region.h"
#include "rvtt-macro-sched.h"
#include "rvtt-macro-desc.h"
#include "rvtt-macro-epoch.h"
#include "rvtt-raw-boundary.h"
#include "rtl-rvtt-macro-planner-int.h"

namespace rvtt_planner {

/* ---------------- Formation: emission from the descriptor ------------ */

/* Return true if the planner may own the macro configuration
   destinations and address-modifier slots for the whole of FN: there is
   no call, no raw asm, and no typed configuration access touching a
   destination owned per C.  The region-scoped checks below are tried
   when this fails.  */

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
	  xtt_effect_set e = rvtt_macro_insn_effects (insn);
	  if (asm_noperands (PATTERN (insn)) >= 0 && e.opaque)
	    /* Raw asm is opaque unless rvtt_insn_effects decoded it as a
	       pure Dst/RWC counter word (rvtt-raw-boundary.cc), which
	       touches no configuration destination.  */
	    return false;
	  if (!e.opaque
	      && ((e.config_dests_written | e.config_dests_read)
		  & c->owned_config_dests))
	    return false;
	}
    }
  return true;
}

/* Configuration ownership for a loop-body REGION, used when the
   function-wide check fails because the function also contains other
   Tensix code (typically a tile's faces processed in a loop inside a
   function full of opaque init code).

   The configuration is emitted at the tail of the loop preheader, which
   dominates every trip's launches.  The preheader's only successor is
   the body (loop_region_preheader checks this), the body's only edges
   are its self-loop and its exit, and this function checks that every
   body instruction is either part of the region or scalar code with no
   call, asm, Tensix instruction or volatile memory reference (the forms
   a raw MMIO instruction push or configuration access would take).
   Configuration writes before the preheader tail are overwritten by the
   prefix; code after the loop exit runs after the drain, as it would
   after a call to a function formed in isolation.  */

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

/* Materialize the 32-bit word IMM into DST (with LV as the previous
   value).

   Immediates are otherwise materialized in GIMPLE
   (gimple-rvtt-immvar.cc), but the planner emits its descriptor program
   in RTL, after that pass.  The cheapest SFPLOADI form is chosen because
   the cost model counts one word for short forms.  */

static void
planner_emit_loadi (rtx dst, rtx lv, rtx imm)
{
  /* Early nonimm pass assures this */
  gcc_assert (CONST_INT_P (imm));

  /* FIXME: we're just moving bits around here, the type of the input value
     doesnt matter.  */
  uint32_t int_imm = INTVAL (imm);
  int new_mod = -1;

  if (int_imm <= 0x7fff || int_imm >= 0xffff8000)
    new_mod = SFPLOADI_MOD0_SHORT;
  else if (int_imm <= 0xffff)
    new_mod = SFPLOADI_MOD0_USHORT;
  else if (!(int_imm & 0xffff))
    {
      imm = GEN_INT (int_imm >> 16);
      new_mod = SFPLOADI_MOD0_FLOATB;
    }
  else if (!(int_imm & 0x1FFF))
    {
      int exp = (int_imm >> 23) & 0xFF;

      if (exp < 127 + 16 && exp >= 127 - 14)
	  {
	    /* Fits in fp16a */
	    imm = GEN_INT (((int_imm >> 13) & 0x3ff)
			   | ((int_imm >> 16) & 0x8000)
			   | ((exp - 0x70) << 10));
	    new_mod = SFPLOADI_MOD0_FLOATA;
	  }
    }

  if (new_mod >= 0)
    emit_insn (gen_rvtt_sfploadi_lv_int (dst, const0_rtx, const0_rtx,
					 const0_rtx, imm,
					 rvtt_gen_rtx_noval (XTT32SImode),
					 lv, GEN_INT (new_mod)));
  else
    {
      /* Build the full literal in two halves.  The UPPER form reads the
         low half from LV, and the pattern ties LV to its result; using
         DST for both avoids a separate temporary and the SFPMOV that
         the tie would otherwise need.  */
      emit_insn (gen_rvtt_sfploadi_lv_int (dst, const0_rtx, const0_rtx,
					   const0_rtx,
					   GEN_INT (int_imm & 0xFFFF),
					   rvtt_gen_rtx_noval (XTT32SImode),
					   lv, GEN_INT (SFPLOADI_MOD0_USHORT)));
      emit_insn (gen_rvtt_sfploadi_lv_int (dst, const0_rtx, const0_rtx,
					   const0_rtx,
					   GEN_INT (int_imm >> 16),
					   rvtt_gen_rtx_noval (XTT32SImode),
					   dst, GEN_INT (SFPLOADI_MOD0_UPPER)));
    }
}

/* Return true if INSN is not a call, not opaque raw asm, and does not
   access a configuration destination owned per C.  */

static bool
planner_scope_insn_clean_p (rtx_insn *insn, const rvtt_macro::caps *c)
{
  if (!NONDEBUG_INSN_P (insn))
    return true;
  if (CALL_P (insn))
    return false;
  xtt_effect_set e = rvtt_macro_insn_effects (insn);
  if (asm_noperands (PATTERN (insn)) >= 0 && e.opaque)
    /* Raw asm is opaque unless decoded as a pure Dst/RWC counter
       word.  */
    return false;
  if (!e.opaque
      && ((e.config_dests_written | e.config_dests_read)
	  & c->owned_config_dests))
    return false;
  return true;
}

/* Region-scoped configuration ownership, used only for CC-template
   programs and only after planner_config_window_ok has failed; it adds
   the straight-line CC-template regions that the window check does not
   cover.  Other regions use only the function-wide check.

   Every insn from SCOPE_BEGIN (straight-line regions: the prefix anchor)
   or from REGION's block head (loop bodies, CONFIG_PREHEADER non-null)
   to the region end must satisfy planner_scope_insn_clean_p; for a loop
   body the preheader's last insn, unless it is a jump, is checked too.

   This is enough because the configuration prefix rewrites every owned
   destination the calendar reads (templates, sequence words, misc; CC
   programs absorb no stride, so no address-modifier slot is read), so
   earlier configuration writes are dead.  Code after the region is not
   checked: the LLK convention is that every SFPLOADMACRO user programs
   its own descriptors before launching.  */

bool
planner_region_config_ownership_ok (const macro_region &region,
				    basic_block config_preheader,
				    rtx_insn *scope_begin,
				    const rvtt_macro::caps *c)
{
  /* Loop bodies are checked from the block head, since every trip
     runs under the configuration materialized in the preheader.  */
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
  /* The prefix is placed at the end of the preheader, before any
     trailing jump, so only that last insn can follow it.  */
  if (config_preheader)
    {
      rtx_insn *tail = BB_END (config_preheader);
      if (tail && !JUMP_P (tail) && !planner_scope_insn_clean_p (tail, c))
	return false;
    }
  return true;
}

/* Prove that hard register VALUE has no use after START before an
   all-lane definition kills it.  */

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

/* Issue cost of materializing the 32-bit configuration word W through
   an LREG (the number of SFPLOADI words; see
   rvtt-delivery-cost-core.h).  */

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

/* Return true if forming RUN_ROWS rows of REGION as one macro run under
   SCHEDULE and DESC is cheaper than issuing them explicitly.  Each run
   must pay for the whole configuration prefix by itself:
     rows*ii + drain + config < rows * explicit-row issues.

   When the crosscall init hoist has already been shown to apply in full
   to this region (IH_STAGE 2), the prefix is not emitted in the callee
   but once per entry of the caller's loop, so it is weighted by the
   caller loop's entry/body profile counts IH_ENTRY/IH_BODY, as in
   loop_profitable_p:
     config * entry + (rows*ii + drain) * body < rows * explicit * body.
   Every run is still charged the whole prefix.  IH_STAGE is 0 unless the
   stage-2 check and the weights both hold.  The decision goes to
   DUMP.  */

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

/* Formation versus replay (-mtt-tensix-macro-ims).  The profitability
   checks above compare the formed calendar with explicit rows pushed by
   the RISC.  When replay is enabled and every row word can be replayed,
   the real alternative is cheaper: the row is recorded once and replayed
   with delivery hidden under execution (rvtt-cost.md).  Formation must
   then also beat a lower bound on that alternative, with record and
   launch delivery charged at zero, using the XTT_REPLAY_COST_* costs;
   otherwise it is refused (replay-delivery-preferred).

   Return true if every word of REGION's row can be replayed.  */

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
  /* The typed Dst-counter separator is a replay barrier, but the Dst
     auto-increment pass, which runs after replay formation, absorbs
     these separators around replay launches.  Without that pass replay
     cannot span rows, and the alternative is the explicit stream the
     other checks already price.  */
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
   at the push rate (or the replay slot rate when planner replay
   delivery, -mtt-tensix-macro-planner-replay, wraps them), drain at the
   slot rate.  */

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

/* Lower bound on the cost of replaying RUN_ROWS rows of REGION: each
   row's words execute at the slot rate with delivery hidden, and record
   and launch words cost nothing.  */

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
   carry the crosscall init-hoist state: at stage 2 both sides are
   weighted by the caller-loop profile counts (the prefix by entry, the
   rest by body).  Return true to form, including when the IMS or replay
   flag is off or the rows cannot be replayed; this check can only
   veto.  Prices and decision go to DUMP.  */

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
  /* At init-hoist stage 2 the prefix executes once per caller-loop
     entry; weight both sides as run_profitable_p does (cross-multiplied,
     no rounding).  The replay alternative has no prefix, so it scales by
     the body count alone.  */
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

/* Loop-body version of ims_arbitrate_run: the descriptor prefix is
   paid once per loop entry (PREHEADER_COUNT) and the launches and
   N_RUNS drains once per trip (BODY_COUNT), against the replay
   alternative per trip.  */

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

/* Store the profile counts of BODY and PREHEADER, scaled down to keep
   products within 64 bits, in *BODY_COUNT and *PREHEADER_COUNT.  The
   ratio is used only for profitability.  Return false when the profile
   gives no usable estimate.  */

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
  /* Shared with the crosscall init-hoist caller weight.  */
  int64_t sb = b, sp = p;
  rvtt_delivery_cost::scale_trip_weight (&sb, &sp);
  *body_count = sb;
  *preheader_count = sp;
  return true;
}

/* Loop-body profitability: the configuration prefix sits in the
   preheader and is paid once per loop entry, while every trip pays each
   run's launch calendar and drain against the explicit rows it
   replaces.  Weighted by the profile counts without rounding:
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

/* Return true if INSN only writes CC and has no other effect.  Whether
   the value written enables all lanes is checked separately by
   cc_enable_all_lanes_proved_p wherever the result is used.  */

static bool
pure_cc_write_insn_p (rtx_insn *insn)
{
  if (!NONDEBUG_INSN_P (insn))
    return false;
  xtt_effect_set e = rvtt_macro_insn_effects (insn);
  return !e.opaque && e.cc_write && !e.cc_read
    && !e.lreg_read && !e.lreg_write
    && !e.config_dests_written && !e.addr_mod_slot_write
    && !e.dst_mem_read && !e.dst_mem_write
    && e.rwc.kind == xtt_rwc_effect_t::NONE;
}

/* Return true if INSN's CC write is the all-lanes SFPENCC word of the
   capability table (rvtt_insn_effects computes this).  */

bool
cc_enable_all_lanes_proved_p (rtx_insn *insn)
{
  if (!rvtt_macro_insn_effects (insn).cc_write_all_lanes)
    return false;
  /* Unattached resets are checked for word shape only; placement must
     independently pass entry_ambient_all_lanes_p.  */
  if (!BLOCK_FOR_INSN (insn))
    return true;
  rvtt_rtl_lane_facts lanes (cfun);
  return lanes.after (insn) == RVTT_RTL_ALL_LANES;
}

/* Return the last Tensix instruction before the loop entered from
   PREHEADER if it is a pure CC write, else null.  When the loop body
   writes no CC, this instruction decides the lane state on entry to
   every trip, so it can stand in for the first row's own enable.  The
   caller must still check the value with
   cc_enable_all_lanes_proved_p.  */

rtx_insn *
preheader_trailing_enable (basic_block preheader)
{
  /* Walk up the unique-predecessor chain from the preheader to the
     first Tensix instruction.  */
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

/* Prove both independent lane facts at the actual placement point.  */
bool
entry_ambient_all_lanes_p (basic_block point_bb, rtx_insn *before,
			  FILE *dump)
{
  rvtt_rtl_lane_facts lanes (cfun);
  bool proven = (before ? lanes.before (before) : lanes.block_out (point_bb))
    == RVTT_RTL_ALL_LANES;
  if (!proven && dump)
    fprintf (dump, "Macro-planner ambient-walk dirty: flags/ROW_MASK unproved\n");
  return proven;
}

/* Return the preheader of loop-body REGION in FN, or null (with the
   refusal written to DUMP).  The loop header must have exactly its
   backedge and one external incoming edge, and the incoming block must
   have no other successor, so the loop runs at least once whenever the
   preheader does and hoisting the all-lanes enable cannot change CC on
   a zero-trip path.  Every Tensix instruction in the body must belong
   to the region, so nothing else can change configuration, CC or
   counter state between the preheader and any launch.  */

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

/* Rewrite the address-mode operand of PAT, a copy of explicit load
   ORIG, to ADDR_MODE; in the compact CC calendar the trailing load's
   auto-increment mode absorbs the deleted separator's stride.  Only
   rvtt_sfpload_lv_int is handled (as in rvtt_dst_access_operands): its
   operands 1..8 are unspec_volatile elements 0..7, so addr_mode
   (operand 8) is element 7.  Return false, changing nothing, for any
   other pattern; formation checks this before emitting anything.  */

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

/* Rewrite the Dst address immediate of PAT, a copy of row insn ORIG, to
   NEW_ADDR.  Rows with immediate address deltas have their explicit
   reloads moved back to rows[0]'s base, and the counter supplies the
   per-row advance.  The two handled Dst patterns (as in
   rvtt_dst_access_operands) both keep the address in unspec element 3.
   Return false, changing nothing, for any other shape; formation tries
   this before emitting anything.  */

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

/* Insert the sequence SEQ at the end of BB, before any trailing
   jump.  */

static void
insert_at_preheader_tail (rtx_insn *seq, basic_block bb)
{
  rtx_insn *tail = BB_END (bb);
  if (tail && JUMP_P (tail))
    emit_insn_before (seq, tail);
  else
    emit_insn_after (seq, tail);
}

/* Derive the effect record (see rvtt-effects.h) of a launch from DESC.
   MACRO_INDEX selects the launch's sequence word; VD is the actual
   (parity-resolved) launch VD index; HIDDEN the launch's hidden
   template-write mask; LMEM/SMEM the carried Dst memory operands.  The
   effects come from the descriptor's SequenceBits and the capability
   table's latencies.  No record is produced for a CC-writing calendar
   (its loads are lane-predicated, so they are not full-lane writes), an
   out-of-range template index, or an undecodable byte.  VD16 staging
   events record an LREG16 write (bit 16 of the mask).  */

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
	  /* A value event writes the launch VD, or LREG16 (the staging
	     register) when its VD16 flag is set.  A store event's VD16
	     flag means it reads LREG16.  */
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
  /* Address-mode RWC effect, as for sfpload: the no-increment mode has
     none; auto-increment deltas are left UNKNOWN.  */
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
   CONFIG_PREHEADER's tail, or (cross-tile epoch) split between
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
		  /* Number of drain NOPs between consecutive rows of this
		     run: the full drain when a fixed-VD value carrier's
		     events are still pending at the next row's launch (see
		     form_region), or the smaller amount computed by
		     rvtt_macro_interrow_drain_tuned under
		     -mtt-tensix-optimize-window-pairing.  */
		  int interrow_drain_slots,
		  /* Descriptor residency (rvtt-macro-desc.cc): omit the
		     descriptor words when an identical program dominates;
		     collect the programming insns for later residency
		     walks; report where the words were programmed.  */
		  bool resident_elide, macro_residency_state *resid,
		  /* Cross-call init hoist: 0 = none, 1 = the descriptor
		     words live in the caller's preheader (retain enable +
		     owned SETC16 per call), 2 = the full prefix lives
		     there (emit nothing).  */
		  int init_hoist_stage,
		  basic_block *config_placement)
{
  const macro_row &first = region.rows[begin];
  rtx_insn *anchor = first.enable ? first.enable : first.insns[0];

  /* Record every insn of SEQ as planner-emitted.  */
  auto collect_emitted = [&] (rtx_insn *seq)
    {
      if (resid)
	for (rtx_insn *i = seq; i; i = NEXT_INSN (i))
	  resid->emitted.add (i);
    };

  if (emit_config)
    {
      /* ENABLE_SRC is the first row's own enable or all-lanes restore,
	 both checked by cc_enable_all_lanes_proved_p, so copying it
	 re-establishes all lanes.  Checking only the first row is enough
	 because region members write CC only in the CC-template roles,
	 whose net effect is that same restore.  A null ENABLE_SRC means
	 the preheader's trailing enable is already in place.  */
      rtx config_lreg = gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST);
      auto config_word = [&] (uint32_t word, unsigned dest)
	{
	  planner_emit_loadi (config_lreg, rvtt_gen_rtx_noval (XTT32SImode),
			      GEN_INT (word));
	  /* sfpwriteconfig_v takes (value, mod, dest); mod 0 is the
	     plain configuration write.  */
	  emit_insn (gen_rvtt_sfpwriteconfig_v (config_lreg, const0_rtx,
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
	  /* The whole prefix is in the caller's loop preheader; nothing
	     to emit per call.  */
	  if (config_placement)
	    *config_placement = nullptr;
	}
      else if (init_hoist_stage == 1)
	{
	  /* Stage 1: the descriptor words are in the caller's preheader;
	     the enable and the owned SETC16 program stay per call.  */
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
	  /* Descriptor residency: an identical program dominates and the
	     owned state is unchanged since, so only the enable and the
	     owned SETC16 program are emitted.  */
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
	  /* Cross-tile configuration epoch: the descriptor words are
	     written once, in the enclosing loop's preheader; the epoch
	     check found nothing in the loop that rewrites them.  The
	     copied all-lanes enable comes first, because the LREG
	     materialization is lane-predicated.  */
	  start_sequence ();
	  emit_insn (copy_rtx (PATTERN (hoist_enable_src)));
	  emit_config_words ();
	  rtx_insn *hoisted = get_insns ();
	  end_sequence ();
	  collect_emitted (hoisted);
	  insert_at_preheader_tail (hoisted, hoist_preheader);
	  if (config_placement)
	    *config_placement = hoist_preheader;

	  /* The per-trip part of the prefix: the enable (the lane state is
	     re-established every tile) and the owned SETC16
	     address-modifier program (the epoch check does not cover MMIO
	     writes to that state).  */
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
	      /* Loop-body region: the prefix executes once, in the
		 preheader (see loop_region_preheader).  */
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
      xtt_effect_set e = rvtt_macro_insn_effects (row0.insns[ix]);
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
     template consuming its value (decoded from the descriptor), or, for
     a load feeding the coalesced lane merge, the shared launch VD.  */
  unsigned explicit_planned[8] = {};
  bool explicit_planned_valid[8] = {};
  for (unsigned ix = 0; ix != row0.insns.length (); ++ix)
    {
      const macro_event &ev = schedule.events[ix];
      xtt_effect_set e = rvtt_macro_insn_effects (row0.insns[ix]);
      if (ev.realization != macro_event::EXPLICIT_INSN || !e.dst_mem_read
	  || ev.is_carrier || ix >= 8)
	continue;
      explicit_planned[ix] = 0;
      for (unsigned jx = 0; jx != row0.insns.length (); ++jx)
	{
	  const macro_event &cons = schedule.events[jx];
	  if (cons.realization == macro_event::CC_COALESCED)
	    {
	      xtt_effect_set ce = rvtt_macro_insn_effects (row0.insns[jx]);
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
	  xtt_effect_set ce = rvtt_macro_insn_effects (row0.insns[jx]);
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
		/* Record the launch's effects (rvtt-effects.h); if they
		   cannot be derived the launch stays opaque.  */
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
		   owned auto-increment address-modifier slot programmed
		   by the prefix.  Formation checked that the rewrite is
		   possible.  */
		if (ev.absorbs_stride)
		  {
		    bool ok = planner_rewrite_load_addr_mode
		      (region.rows[r].insns[ix], pat,
		       c->auto_increment_dst2_addr_mode);
		    gcc_assert (ok);
		  }
		/* Immediate-delta row: move the copied explicit Dst
		   access back to rows[0]'s base; the counter supplies
		   the per-row advance.  Formation checked the rewrite
		   for every such access.  */
		if (region.imm_stride && region.rows[r].imm_delta)
		  {
		    xtt_effect_set fe
		      = rvtt_macro_insn_effects (region.rows[r].insns[ix]);
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
      /* A CC-template program keeps the row's separator: its issue
	 slot is where the restore becomes visible (macro_cc_model), so
	 the next row starts with all lanes enabled.  */
      if (desc.keep_separator && row.separator)
	emit_insn (copy_rtx (PATTERN (row.separator)));
      /* Drain between consecutive rows (the run-end drain below covers
	 the end of the run).  Launch plus drain is the per-row calendar,
	 and spacing in issue slots is a lower bound on spacing in
	 cycles.  */
      if (r + 1 != end)
	for (int d = 0; d != interrow_drain_slots; ++d)
	  emit_insn (gen_rvtt_sfpnop ());
    }
  /* The run-end drain (core_drain_slots over the descriptor's
     SequenceBits delays).  Under -mtt-tensix-optimize-drain-schedule it
     is omitted at a run boundary inside the region when the following
     instructions cannot conflict with the events still in flight
     (rvtt_macro_drain_boundary_elidable, rtl-rvtt-schedule.cc); the
     final run always keeps it.  */
  if (emit_drain)
    for (int d = 0; d != desc.drain_slots; ++d)
      emit_insn (gen_rvtt_sfpnop ());
  rtx_insn *replacement = get_insns ();
  end_sequence ();
  /* Record the emitted calendar as planner-emitted, so the residency
     walks do not refuse on the planner's own launches, whose effects
     depend on the descriptor.  This must happen before insertion, so
     the walk stops at the end of the sequence.  */
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

/* Cross-call init hoist, callee side: check that no instruction of FN
   outside REGION can disturb the hoisted state or depend on the
   per-call prefix: no call, no unknown asm or Tensix instruction, no CC
   write other than the all-lanes enable, no configuration, LREG or Dst
   effect, and no scalar memory store (which could deliver a word).
   Pure RWC counter words and scalar register and branch code are fine.
   Return a refusal name and the offending insn in *WHY_INSN, or null
   when clean.  */

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
	  xtt_effect_set e = rvtt_macro_insn_effects (insn);
	  if (!e.opaque)
	    {
	  /* A canonical zero LaneConfig assignment touches none of the
	     descriptor, SETC16 or LREG state being moved.  Keep this reset
	     in the callee: the cross-call service separately proves zero
	     LaneConfig at the caller's actual placement point.  Thus the
	     moved prefix and retained reset agree on the identity mapping;
	     no arbitrary or nonzero configuration write is admitted.  */
	  if (rvtt_rtl_lane_effect_of (insn) == LANES_ROW_MASK_CLEAR)
	    continue;
	      if (e.cc_write && e.cc_write_all_lanes && !e.cc_read
		  && !e.lreg_read && !e.lreg_write
		  && !e.config_dests_written && !e.config_dests_read
		  && !e.addr_mod_slot_write
		  && !e.dst_mem_read && !e.dst_mem_write)
		continue;	/* re-establishes all lanes */
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

} /* namespace rvtt_planner */
