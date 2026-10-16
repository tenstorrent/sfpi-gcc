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

   Choose the delivery shape of a counted SFPU row loop: how many copies
   of the row the body holds (the unroll factor U), and whether the words
   reach the machine pushed one by one by the RISC, as a recorded replay
   window played back by one-word launches, or as a record hoisted out of
   the loop.  Pushed words cost about a cycle each, launches add a
   boundary cost and records cost their words (constants in
   rvtt-cost.md), so the best shape differs from loop to loop.

   For each single-block innermost loop with a known trip count, the pass
   enumerates every pair of unroll factor U and replay payload rows R,
   predicts the shape the later replay and replay-hoist passes will form
   for it (using the same pricing functions they use, rvtt-timing.h),
   prices it with the delivery-cost table, and picks the cheapest with
   the branch-and-bound solver rvtt_bnb_delivery_shape (rvtt-bnb.cc).
   A non-rolled winner is requested by setting loop->unroll, as
   gimple-rvtt-replay-unroll.cc does; a rolled winner sets
   loop->unroll = 1 so that neither that pass nor the generic unroller
   revisits the loop.  Refused loops are left alone.

   Loop admission uses the same row scan as the fixed-factor pass
   (rvtt_replay_unroll_row_words) and the shared trip-count helper
   (rvtt_loop_trips_gimple), and additionally refuses a row containing an
   instruction without a known latency (delivery_latency_class).

   The pass cannot stop the replay-hoist pass from forming a window in a
   loop where it predicts the rolled explicit form would be cheaper; such
   loops are reported in the dump
   (delivery-shape-downstream-override-required).

   Only loop annotations change; the computation is unaffected.  The
   trade-off is that of procedural abstraction (Fraser, Myers and Wendt,
   "Analyzing and compressing assembly code", 1984), with a REPLAY launch
   as the call and the recorded window as the body.  */

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
#include "rvtt-trips.h"
#include "rvtt.h"
#include "rvtt-refuse.h"
#include "rvtt-schedule.h"
#include "rvtt-delivery-cost.h"

namespace {

/* Latency class of INSND for the execution term of the delivery model:
     0 -- issues at full pace;
     1 -- next-slot acceptance stall, one extra slot per occurrence,
	  charged in both execution estimates;
     2 -- latency-1 family (mad and LUT rows): the delivery table treats
	  the stall as hidden, but the RTL replay-hoist interlock model
	  charges it one slot, so it is charged only in the prediction of
	  that pass (ds_exec);
    -1 -- latency unknown; the loop is refused.

   The classes follow rvtt-cost.md's xtt_result_latency and
   xtt_next_slot_stall attributes per instruction rather than per mod;
   a wrong class only changes the predicted cost.  The CC family
   (SFPSETCC/SFPENCC/SFPCOMPC/SFPPUSHC/SFPPOPC) and SFPARECIP have no
   next-cycle constraint on either architecture.  Structured
   pre-expansion forms (sfpxloadi, sfpxpred, ...) lower to members of the
   loadi/iadd/CC classes.  */

static int
delivery_latency_class (const rvtt_insn_data *insnd)
{
  switch (insnd->id)
    {
    /* Next-slot acceptance stall (SFPSWAP.md).  */
    case rvtt_insn_data::sfpswap:
    case rvtt_insn_data::sfpswap_indexed:
      return 1;

    /* Loads, stores, immediates, simple unit, round unit, iadd/divp2,
       CC family, SFPARECIP, typed row steps, structured pre-expansion
       forms and plumbing.  */
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
    case rvtt_insn_data::sfpdivp2:
    case rvtt_insn_data::sfpdivp2_lv:
    case rvtt_insn_data::sfpcast:
    case rvtt_insn_data::sfpcast_lv:
    case rvtt_insn_data::sfpstochrnd:
    case rvtt_insn_data::sfpstochrnd_lv:
    case rvtt_insn_data::sfpstochrnd_descale_i:
    case rvtt_insn_data::sfpstochrnd_descale_i_lv:
    case rvtt_insn_data::sfpstochrnd_descale_v:
    case rvtt_insn_data::sfpstochrnd_descale_v_lv:
    case rvtt_insn_data::sfpsetexp_v:
    case rvtt_insn_data::sfpsetexp_v_lv:
    case rvtt_insn_data::sfpsetexp_i:
    case rvtt_insn_data::sfpsetexp_i_lv:
    case rvtt_insn_data::sfpsetman_v:
    case rvtt_insn_data::sfpsetman_v_lv:
    case rvtt_insn_data::sfpsetman_i:
    case rvtt_insn_data::sfpsetman_i_lv:
    case rvtt_insn_data::sfpsetsgn_v:
    case rvtt_insn_data::sfpsetsgn_v_lv:
    case rvtt_insn_data::sfpsetsgn_i:
    case rvtt_insn_data::sfpsetsgn_i_lv:
    case rvtt_insn_data::sfparecip:
    case rvtt_insn_data::sfparecip_lv:
    case rvtt_insn_data::sfpsetcc:
    case rvtt_insn_data::sfpencc:
    case rvtt_insn_data::sfpencc_all_lanes:
    case rvtt_insn_data::sfpcompc:
    case rvtt_insn_data::sfppushc:
    case rvtt_insn_data::sfppopc:
    case rvtt_insn_data::sfpnop:
    case rvtt_insn_data::ttincrwc:
    case rvtt_insn_data::ttdstface:
    case rvtt_insn_data::sfpxpred:
    case rvtt_insn_data::sfpxlogic:
    case rvtt_insn_data::sfpxcond:
    /* sfpxcmp is not here: the float and integer compares share one
       builtin, and the float form costs 2 (it lowers through the mad
       unit) while the integer form costs 0.  The two cannot be told
       apart here, so it takes the 2 below -- an over-estimate of the
       execution term, which makes hoisting decisions more conservative
       rather than less.  */
    case rvtt_insn_data::sfpreadlreg:
    case rvtt_insn_data::sfpassign:
    case rvtt_insn_data::sfpassign_lv:
    case rvtt_insn_data::sfpnovalue:
      return 0;

    /* Latency-1 family (mad rows and LUT).  */
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

    /* Structured float compares lower to an SFPMAD-family
       instruction, so they are treated like the mad family.  */
    case rvtt_insn_data::sfpxcmp:
      return 2;

    /* No single latency (SFPLUTFP32 depends on the mode, SFPSHFT2 has
       mod-dependent next-cycle register constraints, ...): refuse.  */
    default:
      return -1;
    }
}

/* Dump name of delivery MODE, matching the rvtt_delivery_mode
   vocabulary declared in rvtt-schedule.h.  */

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
    rvtt_refuse_by_name (name, dump_file,
			 "delivery-shape: refused (%s) loop %d",
			 name, loop->num);
    if (dump_file)
      {
	if (detail)
	  fprintf (dump_file, ": %s", detail);
	fprintf (dump_file, "\n");
      }
  }

  /* Scan + solve one candidate loop.  */
  void process (function *fun, class loop *loop)
  {
    if (loop->inner || loop->num_nodes != 1)
      {
	refuse (loop, "delivery-shape-body-not-flat", NULL);
	return;
      }
    if (loop->unroll)
      /* Never override a user or earlier-pass annotation.  */
      return;

    unsigned HOST_WIDE_INT trips;
    if (!rvtt_loop_trips_gimple (loop, &trips))
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
    /* The tables and flags the replay-hoist pass will use, so the
       solver predicts its decisions.  */
    prob.dcost = rvtt_dcost_table ();
    prob.ds_hoist_min_benefit = rvtt_dcost_replay_hoist_min_benefit ();
    prob.hoist_enabled = riscv_tt_opt_replay_hoist != 0;
    prob.record_hoist_enabled = riscv_tt_opt_replay_record_hoist > 0;
    prob.completion_guard = riscv_tt_replay_hoist_completion_guard > 0;
    prob.autoincr_enabled = riscv_tt_opt_dst_autoincr != 0;
    prob.autoincr_setup_x100 = rvtt_dcost_autoincr_setup_cost_x100 ();

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
	  rvtt_refuse (RVTT_REF_DELIVERY_SHAPE_WINDOW_BUDGET, dump_file,
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

    /* Rolled is best: setting unroll to 1 stops the fixed-factor pass
       and the generic unroller from unrolling the loop.  */
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
	    /* The replay-hoist pass is predicted to form a window in this
	       loop.  If the explicit rolled form is cheaper, report it:
	       this pass cannot prevent that.  */
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
  OPTGROUP_OTHER,
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
	rvtt_refuse (RVTT_REF_DELIVERY_SHAPE_QSR_UNPROVEN, dump_file,
		     "delivery-shape: refused"
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

} /* anonymous namespace */

/* The pass runs after the gimple passes that shape the loop body and
   before gimple-rvtt-replay-unroll.cc, whose decision an annotation here
   takes precedence over.  */

gimple_opt_pass *
make_pass_rvtt_delivery_shape (gcc::context *ctxt)
{
  return new pass_rvtt_delivery_shape (ctxt);
}
