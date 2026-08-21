/* Priced delivery-shape arbitration for counted SFPU row loops.
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

/* -mtt-tensix-optimize-delivery-shape (default off).

   THE PROBLEM.  The delivery shape of a counted SFPU row loop -- how
   many textual copies of the row exist, and whether the machine
   receives them as straight RISC-pushed words, as one recorded replay
   window re-played by one-word launches, or as a record hoisted out of
   the loop -- is decided today by a fixed-factor unroll request
   (gimple-rvtt-replay-unroll.cc, XTT_REPLAY_LOOP_UNROLL_FACTOR) and by
   the downstream formers' own local gates.  Measured anatomy (lane EE,
   laneEE-evidence-20260821: fourteen silicon rows closed within ~3%)
   shows the shape choice is a real, priced trade -- straight-pushed
   words cost ~1.0 cycle each, launches carry a measured 1.3..1.8-cycle
   serial-chain boundary cost, records cost their words -- and that the
   winning shape differs per loop (sqrt-fresh measures best rolled
   while its rsqrt twin measures best windowed).

   THE MECHANISM.  One solver, one model: per proven-trip counted
   single-block SFPU row loop, enumerate the whole discrete shape
   lattice {unroll factor U} x {payload rows R}, PREDICT the shape the
   downstream silicon-calibrated machinery materializes for each U (a
   read-only mirror of the replay former's grouping and the
   replay-hoist gate's published rvtt-cost.md model -- prediction,
   never re-pricing), price every predicted shape with the measured
   lane-EE delivery table, and take the exact argmin
   (rvtt_bnb_delivery_shape, the vendored exact branch-and-bound home,
   rvtt-bnb.cc).  A winning non-rolled shape is requested through
   exactly the annotation the fixed-factor pass uses (loop->unroll);
   an affirmative rolled selection annotates loop->unroll = 1, owning
   the decision slot; every refusal leaves the function byte-identical.

   ADMISSION is the fixed-factor pass's typed census, consumed through
   its exported vocabulary (rvtt_replay_unroll_row_words /
   rvtt_replay_unroll_counted_trips, rvtt-protos.h) so the two
   admissions cannot drift, PLUS the audited-latency mirror below:
   a row member with no audited latency fact makes the row's execution
   term unpriceable and the loop refuses by name
   (delivery-shape-exec-term-unaudited).

   MODEL SEAMS (documented, stubbed to current-model values):
     - lane EB's dst-autoincr body-length pricing term is not yet
       pushed; the solver models no autoincr setup cost (current-model
       value 0) and consumes only the autoincr ENABLE bit for the
       downstream mirror's saturation run.  When EB's term lands it
       joins the measured table.
     - lane EC's record-hoist (record-once one level further out) is
       not yet pushed; the downstream mirror models only the hoist
       machinery present at this pin (rtl-rvtt-replay.cc counted-loop
       and re-record branches).  When EC lands, its wider hoist scope
       joins the mirror.
     - where the modeled winner is a ROLLED shape but the downstream
       hoist's own gate is predicted to form a window anyway (the
       ceil-fresh class), this pass has no channel to suppress that
       pass; the disagreement is dumped by name
       (delivery-shape-downstream-override-required) as the wiring
       seam.

   No operation identity, opcode calendar, coefficient value, or
   instruction-word fingerprint participates in any decision.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "cfghooks.h"
#include "cfgloop.h"
#include "tree-cfg.h"
#include "insn-constants.h"
#include "rvtt-protos.h"
#include "rvtt.h"
#include "rvtt-schedule.h"

namespace {

/* Audited-latency mirror for the delivery model's execution term.
   Values: 0 = audited, execution measures at issue pace (the lane-EE
   closure prices exec == slots); 1 = audited next-slot ACCEPTANCE
   stall (one extra slot per occurrence, architectural, charged in
   BOTH exec estimates); 2 = audited latency-1 family (mad/LUT rows):
   the lane-EE closure measures these stalls as ABSORBED (not charged
   in the measured exec term), while the downstream RTL gate's
   interlock estimate charges them one slot each (verified against the
   recorded pin-13 refusal arithmetic: hardshrink's -383 reproduces
   exactly at exec_ilk = 9 words + 1 mad stall), so they are charged
   only in the downstream-mirror exec; -1 = no audited latency fact,
   the row's execution term is unpriceable and the loop must refuse by
   name.

   Provenance mirrors rvtt-cost.md's xtt_result_latency /
   xtt_next_slot_stall audit blocks (D3 + follow-ups), the class-level
   envelope (per-mod refinements are enforced by the RTL consumers;
   a mis-refined mod here can only shift the modeled delta, never
   semantics), plus two page audits recorded in the rvtt-cost.md
   delivery-shape section: the CC family (SFPSETCC/SFPENCC/SFPCOMPC/
   SFPPUSHC/SFPPOPC) and SFPARECIP carry no next-cycle rule on either
   architecture's page (the audited latency-0 page convention).
   Structured pre-expansion forms (sfpxvif, the sfpxcmp / sfpxiadd
   families, sfpxloadi, ...) lower to members of the audited
   loadi/iadd/CC classes.  */

static int
delivery_latency_class (const rvtt_insn_data *insnd)
{
  switch (insnd->id)
    {
    /* Audited next-slot acceptance stall (SFPSWAP.md).  */
    case rvtt_insn_data::sfpswap:
    case rvtt_insn_data::sfpswap_indexed:
      return 1;

    /* Audited classes: loads, stores, immediates, simple unit, round
       unit, iadd/divp2 rows, mad family and LUT (latency-1 rows whose
       stalls the lane-EE closure measures as absorbed), CC family and
       SFPARECIP (page audits), typed row steps, structured
       pre-expansion forms, plumbing.  */
    case rvtt_insn_data::sfpload:
    case rvtt_insn_data::sfpload_lv:
    case rvtt_insn_data::sfploaddiscard:
    case rvtt_insn_data::sfploadsrcs:
    case rvtt_insn_data::sfploadsrcs_lv:
    case rvtt_insn_data::sfpstore:
    case rvtt_insn_data::sfpstoresrcs:
    case rvtt_insn_data::sfploadi:
    case rvtt_insn_data::sfploadi_lv:
    case rvtt_insn_data::sfpxloadi:
    case rvtt_insn_data::sfpmov:
    case rvtt_insn_data::sfpmov_lv:
    case rvtt_insn_data::sfpexexp:
    case rvtt_insn_data::sfpexexp_lv:
    case rvtt_insn_data::sfpexman:
    case rvtt_insn_data::sfpexman_lv:
    case rvtt_insn_data::sfpabs:
    case rvtt_insn_data::sfpabs_lv:
    case rvtt_insn_data::sfplz:
    case rvtt_insn_data::sfplz_lv:
    case rvtt_insn_data::sfpand:
    case rvtt_insn_data::sfpand_lv:
    case rvtt_insn_data::sfpor:
    case rvtt_insn_data::sfpor_lv:
    case rvtt_insn_data::sfpxor:
    case rvtt_insn_data::sfpxor_lv:
    case rvtt_insn_data::sfpshft_v:
    case rvtt_insn_data::sfpshft_v_lv:
    case rvtt_insn_data::sfpshft_i:
    case rvtt_insn_data::sfpshft_i_lv:
    case rvtt_insn_data::sfpiadd_v:
    case rvtt_insn_data::sfpiadd_v_lv:
    case rvtt_insn_data::sfpiadd_i:
    case rvtt_insn_data::sfpiadd_i_lv:
    case rvtt_insn_data::sfpxiadd_v:
    case rvtt_insn_data::sfpxiadd_i:
    case rvtt_insn_data::sfpxiadd_i_lv:
    case rvtt_insn_data::sfpdivp2:
    case rvtt_insn_data::sfpdivp2_lv:
    case rvtt_insn_data::sfpcast:
    case rvtt_insn_data::sfpcast_lv:
    case rvtt_insn_data::sfpstochrnd_i:
    case rvtt_insn_data::sfpstochrnd_i_lv:
    case rvtt_insn_data::sfpstochrnd_v:
    case rvtt_insn_data::sfpstochrnd_v_lv:
    case rvtt_insn_data::sfpsetexp_v:
    case rvtt_insn_data::sfpsetexp_v_lv:
    case rvtt_insn_data::sfpsetman_v:
    case rvtt_insn_data::sfpsetman_v_lv:
    case rvtt_insn_data::sfpsetsgn_v:
    case rvtt_insn_data::sfpsetsgn_v_lv:
    case rvtt_insn_data::sfparecip:
    case rvtt_insn_data::sfparecip_lv:
    case rvtt_insn_data::sfpsetcc_i:
    case rvtt_insn_data::sfpsetcc_v:
    case rvtt_insn_data::sfpencc:
    case rvtt_insn_data::sfpencc_all_lanes:
    case rvtt_insn_data::sfpcompc:
    case rvtt_insn_data::sfppushc:
    case rvtt_insn_data::sfppopc:
    case rvtt_insn_data::sfpnop:
    case rvtt_insn_data::ttincrwc:
    case rvtt_insn_data::ttdstface:
    case rvtt_insn_data::sfpxvif:
    case rvtt_insn_data::sfpxbool:
    case rvtt_insn_data::sfpxcondb:
    case rvtt_insn_data::sfpxcondi:
    case rvtt_insn_data::sfpxicmps:
    case rvtt_insn_data::sfpxicmpv:
    case rvtt_insn_data::sfpreadlreg:
    case rvtt_insn_data::sfpassign:
    case rvtt_insn_data::sfpassign_lv:
    case rvtt_insn_data::sfpnovalue:
      return 0;

    /* Audited latency-1 family (mad rows + LUT): absorbed in the
       measured exec, charged in the downstream-mirror exec.  */
    case rvtt_insn_data::sfpmul:
    case rvtt_insn_data::sfpmul_lv:
    case rvtt_insn_data::sfpmuli:
    case rvtt_insn_data::sfpmuli_lv:
    case rvtt_insn_data::sfpadd:
    case rvtt_insn_data::sfpadd_lv:
    case rvtt_insn_data::sfpaddi:
    case rvtt_insn_data::sfpaddi_lv:
    case rvtt_insn_data::sfpmad:
    case rvtt_insn_data::sfpmad_lv:
    case rvtt_insn_data::sfpmul24:
    case rvtt_insn_data::sfpmul24_lv:
    case rvtt_insn_data::sfplut:
      return 2;

    /* Structured float compares lower through the mad unit (the
       expanded compare-vs-operand is an SFPMAD-family member in the
       final stream -- lane EE anatomy rows 1/2; the recorded pin-13
       hoist-refusal arithmetic requires exec_ilk = words + 1 on
       exactly these bodies).  Same treatment as the mad family:
       absorbed in the measured exec, one slot in the downstream
       mirror.  Integer compares lower to the audited iadd class and
       stay latency-0.  */
    case rvtt_insn_data::sfpxfcmps:
    case rvtt_insn_data::sfpxfcmpv:
      return 2;

    /* No audited latency fact (SFPLUTFP32's per-mode split, SFPSHFT2's
       mod-dependent next-cycle register constraints, the compare
       set-dest mod-conditional audit, immediate setexp/setman/setsgn
       forms, select/nonlinear/not, ...): the execution term is
       unpriceable -- refuse by name.  */
    default:
      return -1;
    }
}

static const char *
delivery_mode_name (rvtt_delivery_mode mode)
{
  switch (mode)
    {
    case rvtt_delivery_mode::rolled_explicit: return "rolled-explicit";
    case rvtt_delivery_mode::rolled_hoisted: return "rolled-hoisted";
    case rvtt_delivery_mode::group_rerecord: return "group-rerecord";
    case rvtt_delivery_mode::group_hoisted: return "group-hoisted";
    case rvtt_delivery_mode::unrolled_explicit: return "unrolled-explicit";
    }
  return "?";
}

class delivery_shape
{
public:
  unsigned n_fired = 0;
  unsigned n_rolled = 0;
  unsigned n_refused = 0;

  void refuse (class loop *loop, const char *name, const char *detail)
  {
    ++n_refused;
    if (dump_file)
      {
	fprintf (dump_file, "delivery-shape: refused (%s) loop %d",
		 name, loop->num);
	if (detail)
	  fprintf (dump_file, ": %s", detail);
	fprintf (dump_file, "\n");
      }
  }

  /* Census + solve one candidate loop.  */
  void process (function *fun, class loop *loop)
  {
    if (loop->inner || loop->num_nodes != 1)
      {
	refuse (loop, "delivery-shape-body-not-flat", NULL);
	return;
      }
    if (loop->unroll)
      /* A user or earlier-pass annotation is on record; never
	 override it.  */
      return;

    unsigned HOST_WIDE_INT trips;
    if (!rvtt_replay_unroll_counted_trips (loop, &trips))
      {
	refuse (loop, "delivery-shape-trip-count-unproven", NULL);
	return;
      }
    if (trips < 2)
      {
	refuse (loop, "delivery-shape-trip-count-unproven",
		"fewer than two trips");
	return;
      }

    basic_block bb = loop->header;
    unsigned words = 0;
    unsigned stall_words = 0;
    unsigned lat1_words = 0;
    unsigned barrier_words = 0;
    bool saw_cond = false;
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
	    || gimple_nop_p (stmt))
	  continue;
	if (gimple_clobber_p (stmt))
	  continue;

	if (dyn_cast <gcond *> (stmt))
	  {
	    saw_cond = true;
	    continue;
	  }

	if (gcall *call = dyn_cast <gcall *> (stmt))
	  {
	    const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
	    if (!insnd)
	      {
		refuse (loop, "delivery-shape-foreign-stmt",
			"non-rvtt call");
		return;
	      }
	    int w = rvtt_replay_unroll_row_words (insnd);
	    if (w < 0)
	      {
		refuse (loop, "delivery-shape-denied-class",
			insnd->name);
		return;
	      }
	    int lat = delivery_latency_class (insnd);
	    if (lat < 0)
	      {
		refuse (loop, "delivery-shape-exec-term-unaudited",
			insnd->name);
		return;
	      }
	    words += w;
	    if (w > 0 && lat == 1)
	      ++stall_words;
	    if (w > 0 && lat == 2)
	      ++lat1_words;
	    /* Typed Dst-step words are xtt_replay=barrier: a replay
	       window can never span them, and the Dst auto-increment
	       pass absorbs them after formation.  */
	    if (insnd->id == rvtt_insn_data::ttincrwc
		|| insnd->id == rvtt_insn_data::ttdstface)
	      ++barrier_words;
	    continue;
	  }

	if (gassign *assign = dyn_cast <gassign *> (stmt))
	  {
	    if (gimple_vuse (stmt) || gimple_vdef (stmt))
	      {
		refuse (loop, "delivery-shape-memory", NULL);
		return;
	      }
	    tree lhs = gimple_assign_lhs (assign);
	    if (TREE_CODE (lhs) != SSA_NAME)
	      {
		refuse (loop, "delivery-shape-foreign-stmt",
			"non-SSA assignment");
		return;
	      }
	    continue;
	  }

	refuse (loop, "delivery-shape-foreign-stmt",
		gimple_code_name[gimple_code (stmt)]);
	return;
      }

    if (!saw_cond)
      {
	refuse (loop, "delivery-shape-body-not-flat",
		"no exit condition in header");
	return;
      }
    if (words == 0)
      {
	refuse (loop, "delivery-shape-row-empty", NULL);
	return;
      }

    rvtt_delivery_problem prob;
    prob.trips = (unsigned) trips;
    prob.row_words = words;
    prob.row_exec = words + stall_words;
    prob.ds_exec = words + stall_words + lat1_words;
    prob.barrier_words = barrier_words;
    prob.control_words = 2;
    prob.max_factor = XTT_REPLAY_LOOP_UNROLL_FACTOR;
    prob.min_sequence = XTT_REPLAY_LOOP_UNROLL_MIN_WORDS;
    prob.capture_slots = XTT_DELIVERY_CAPTURE_SLOTS;
    prob.max_words = XTT_REPLAY_LOOP_UNROLL_MAX_WORDS;
    prob.word = XTT_DELIVERY_WORD_X100;
    prob.boundary_lb = XTT_DELIVERY_BOUNDARY_LB_X100;
    prob.boundary_ub = XTT_DELIVERY_BOUNDARY_UB_X100;
    prob.min_benefit
      = riscv_tt_delivery_shape_min_benefit >= 0
	  ? (unsigned) riscv_tt_delivery_shape_min_benefit
	  : (unsigned) XTT_DELIVERY_SHAPE_MIN_BENEFIT;
    prob.ds_push = XTT_REPLAY_COST_RISC_PUSH_X100;
    prob.ds_slot = XTT_REPLAY_COST_REPLAY_SLOT_X100;
    prob.ds_turnaround = XTT_REPLAY_COST_TURNAROUND_X100;
    prob.ds_record_overhead = XTT_REPLAY_COST_RECORD_OVERHEAD_X100;
    prob.ds_hoist_min_benefit
      = riscv_tt_replay_hoist_min_benefit >= 0
	  ? riscv_tt_replay_hoist_min_benefit
	  : XTT_REPLAY_HOIST_MIN_BENEFIT;
    prob.hoist_enabled = riscv_tt_opt_replay_hoist != 0;
    prob.autoincr_enabled = riscv_tt_opt_dst_autoincr != 0;

    rvtt_delivery_solution sol = rvtt_bnb_delivery_shape (prob);
    if (sol.status != rvtt_solver_status::optimal)
      {
	refuse (loop, "delivery-shape-solver", sol.diagnostic);
	return;
      }

    if (dump_file)
      {
	fprintf (dump_file,
		 "delivery-shape: loop %d trips " HOST_WIDE_INT_PRINT_UNSIGNED
		 " row words %u (safe %u) exec %u (nodes %u)\n",
		 loop->num, trips, words, words - barrier_words,
		 prob.row_exec, sol.solver_nodes);
	for (const rvtt_delivery_candidate &c : sol.candidates)
	  fprintf (dump_file,
		   "delivery-shape:   candidate U=%u R=%u %s cost["
		   "%" PRId64 "..%" PRId64 "] cs\n",
		   c.factor, c.payload_rows, delivery_mode_name (c.mode),
		   c.cost_blb, c.cost_bub);
	if (sol.window_infeasible)
	  fprintf (dump_file,
		   "delivery-shape: window legs refused"
		   " (delivery-shape-window-budget) loop %d:"
		   " no payload fits [%u..%u] slots\n",
		   loop->num, prob.min_sequence, prob.capture_slots);
      }

    if (sol.selected.factor >= 2
	&& sol.benefit_min >= (int64_t) prob.min_benefit)
      {
	loop->unroll = (unsigned short) sol.selected.factor;
	fun->has_unroll = true;
	++n_fired;
	if (dump_file)
	  fprintf (dump_file,
		   "delivery-shape: requested unroll %u of loop %d"
		   " (payload rows %u, mode %s, benefit-min %" PRId64
		   " cs/entry)\n",
		   sol.selected.factor, loop->num,
		   sol.selected.payload_rows,
		   delivery_mode_name (sol.selected.mode),
		   sol.benefit_min);
	return;
      }

    /* Affirmative rolled selection: own the decision slot (the
       annotation blocks the fixed-factor request pass and the generic
       unroller from re-deciding), byte-identical object code.  */
    loop->unroll = 1;
    ++n_rolled;
    if (dump_file)
      {
	fprintf (dump_file,
		 "delivery-shape: selected rolled (%s) for loop %d"
		 " (best non-rolled benefit-min %" PRId64
		 " cs/entry, threshold %u)\n",
		 delivery_mode_name (sol.rolled.mode), loop->num,
		 sol.selected.factor >= 2 ? sol.benefit_min : (int64_t) 0,
		 prob.min_benefit);
	if (sol.rolled.mode == rvtt_delivery_mode::rolled_hoisted)
	  {
	    /* The downstream hoist is predicted to window this rolled
	       loop on its own gate; if the measured table prices the
	       explicit rolled form cheaper, this pass has no channel
	       to suppress that pass -- name the seam.  */
	    int64_t explicit_cost
	      = (int64_t) prob.trips
		* ((int64_t) (prob.row_exec > words + prob.control_words
			      ? prob.row_exec
			      : words + prob.control_words) * 100);
	    if (explicit_cost < sol.rolled.cost_bub)
	      fprintf (dump_file,
		       "delivery-shape: note"
		       " (delivery-shape-downstream-override-required)"
		       " loop %d: modeled explicit %" PRId64
		       " cs beats predicted hoisted window [%" PRId64
		       "..%" PRId64 "] cs; no suppression channel"
		       " exists at this slot\n",
		       loop->num, explicit_cost, sol.rolled.cost_blb,
		       sol.rolled.cost_bub);
	  }
      }
  }
};

const pass_data pass_data_rvtt_delivery_shape =
{
  GIMPLE_PASS,
  "rvtt_delivery_shape",
  OPTGROUP_NONE,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_rvtt_delivery_shape : public gimple_opt_pass
{
public:
  pass_rvtt_delivery_shape (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_delivery_shape, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_delivery_shape > 0;
  }

  unsigned execute (function *fun) final override
  {
    if (TARGET_XTT_TENSIX_QSR)
      {
	if (dump_file)
	  fprintf (dump_file, "delivery-shape: refused"
		   " (delivery-shape-qsr-unproven)\n");
	return 0;
      }
    delivery_shape ctx;
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    for (auto loop : loops_list (fun, LI_ONLY_INNERMOST))
      ctx.process (fun, loop);
    loop_optimizer_finalize ();
    if (dump_file)
      fprintf (dump_file,
	       "delivery-shape: fires=%u rolled=%u refusals=%u\n",
	       ctx.n_fired, ctx.n_rolled, ctx.n_refused);
    /* Annotation only; no IL edits.  */
    return 0;
  }
};

} // anonymous namespace

gimple_opt_pass *
make_pass_rvtt_delivery_shape (gcc::context *ctxt)
{
  return new pass_rvtt_delivery_shape (ctxt);
}
