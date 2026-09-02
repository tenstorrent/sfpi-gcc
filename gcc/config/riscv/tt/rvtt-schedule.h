/* Canonical scheduling model for Tensix SFPU regions.
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

#ifndef GCC_RVTT_SCHEDULE_H
#define GCC_RVTT_SCHEDULE_H

#include <cstdint>
#include <utility>
#include <vector>

#include "rvtt-delivery-cost-core.h"

/* This representation intentionally contains no GIMPLE or RTL pointers.
   Stable integer IDs make solver output independently checkable and allow
   the same small model to be exercised outside a compiler pass.  */
struct rvtt_sched_value
{
  /* -1 denotes a value live on entry to the region.  */
  int def = -1;
  std::vector<unsigned> uses;
  bool live_out = false;
};

struct rvtt_sched_problem
{
  unsigned operation_count = 0;
  unsigned register_capacity = 8;
  std::vector<std::pair<unsigned, unsigned>> dependencies;
  std::vector<rvtt_sched_value> values;
  /* Preferred issue slot for each operation.  The deterministic list
     scheduler supplies a known-good incumbent when it found one; the MILP
     may deviate from it to satisfy capacity.  */
  std::vector<unsigned> preferred_slot;
  bool preferred_feasible = false;
};

enum class rvtt_solver_status
{
  unavailable,
  capped,
  optimal,
  infeasible,
  nonoptimal,
  invalid_model,
  internal_error
};

struct rvtt_solver_solution
{
  rvtt_solver_status status = rvtt_solver_status::unavailable;
  std::vector<unsigned> order;
  unsigned solver_nodes = 0;
  const char *diagnostic = "none";
  /* Result of cross-checking the primary exact solver against the
     optional lp_solve backend: "none" (lp_solve not configured),
     "agree", "disagree", or "skipped" (either side capped or errored,
     so terminal verdicts cannot be compared).  */
  const char *cross_check = "none";
};

/* The optional lp_solve adapter (rvtt-lpsolve.cc).  Compiled in only
   under --with-lp-solve; used exclusively as an independent
   cross-check of the built-in solver, never as the primary backend,
   so production code generation is byte-identical whether or not it
   was configured.  */
extern bool rvtt_lpsolve_available ();
extern rvtt_solver_solution
rvtt_lpsolve_schedule (const rvtt_sched_problem &);

/* The built-in exact branch-and-bound solver (rvtt-bnb.cc).  Always
   compiled; dependency-free; deterministic.  */
extern rvtt_solver_solution
rvtt_bnb_schedule (const rvtt_sched_problem &);

/* Solver entry points for passes: the built-in exact solver, with the
   lp_solve cross-check folded in when configured.  */
extern bool rvtt_solver_available ();
extern const char *rvtt_solver_backend_name ();
extern rvtt_solver_solution
rvtt_solve_schedule (const rvtt_sched_problem &);

extern const char *rvtt_solver_status_name (rvtt_solver_status);

/* ---------------------------------------------------------------------
   Delivery-shape arbitration model (lane EG).

   One proven-trip counted single-block SFPU row loop; the solver picks
   the unroll factor U over the whole discrete candidate lattice by
   exact minimization of modeled cycles.  For each U it first PREDICTS
   which delivery shape the downstream, silicon-calibrated machinery
   will materialize (the always-on replay former groups textual copies
   into re-record + launches; the counted-loop/re-record hoist gate of
   rtl-rvtt-replay.cc may lift the record out of the loop) by mirroring
   that gate's own published model (rvtt-cost.md, downstream constants,
   read-only -- no re-pricing there), then prices the PREDICTED shape
   with the measured delivery table below.

   Like the scheduling model above, this representation carries no
   GIMPLE pointers: the same small model is independently checkable
   outside a compiler pass.  All costs are centislots (hundredths of a
   Tensix issue slot), int64.

   Measured delivery table (a 14-row hardware study; every row's
   modeled cost reproduced within ~3% of the measurement):
     - an issue-bound leg costs ~1.0 cycle per delivered word
       (WORD = 100 cs; scalar loop-control pairs partially dual-issue
       fold -- the closure's documented <= 7% slack);
     - a replay-delivered leg costs its payload slots at 1.0 each,
       plus its record passes at (1 + slots) words (additive: record
       delivery measures exposed), plus a measured per-launch BOUNDARY
       cost of 1.3..1.8 cycles on serial-chain windows (exposed
       re-issue; the interval is carried, not averaged); its
       loop-control delivery measures hidden under the execution
       backlog;
     - execution of an audited row costs its slot count: the mad-family
       latency-1 stalls measure as absorbed on all four chain-heavy
       anatomy rows (exec == slots), while the audited SFPSWAP
       next-slot acceptance stall is architectural and is charged one
       extra slot per occurrence.

   A non-rolled shape is requested only when its modeled benefit
   clears the threshold at BOTH ends of the boundary interval; rows
   containing a producer with no audited latency fact refuse by name
   upstream (delivery-shape-exec-term-unaudited).  */

struct rvtt_delivery_problem
{
  unsigned trips = 0;		/* proven constant trip count, >= 2 */
  unsigned row_words = 0;	/* estimated delivered words per row */
  unsigned row_exec = 0;	/* slots per row incl. acceptance stalls
				   (measured table: latency-1 stalls
				   absorbed) */
  unsigned ds_exec = 0;		/* downstream-mirror slots per row: the
				   RTL gate's interlock estimate charges
				   audited latency-1 producers one slot
				   (verified against the recorded pin-13
				   refusal arithmetic) */
  unsigned barrier_words = 0;	/* replay-barrier words per row (typed
				   Dst steps: TTINCRWC/TTDSTFACE are
				   xtt_replay=barrier, so a window can
				   never span them; the Dst
				   auto-increment pass absorbs them
				   AFTER replay formation) */
  unsigned control_words = 2;	/* delivered loop-control words/trip */
  unsigned max_factor = 8;	/* XTT_REPLAY_LOOP_UNROLL_FACTOR */
  unsigned min_sequence = 4;	/* replay former's MIN_SEQUENCE */
  unsigned capture_slots = 32;	/* replay buffer slots */
  unsigned max_words = 256;	/* XTT_REPLAY_LOOP_UNROLL_MAX_WORDS */
  /* Measured delivery rates (centislots), lane EE table.  */
  unsigned word = 100;		/* XTT_DELIVERY_WORD_X100 */
  unsigned boundary_lb = 130;	/* XTT_DELIVERY_BOUNDARY_{LB,UB}_X100 */
  unsigned boundary_ub = 180;
  unsigned min_benefit = 60;	/* XTT_DELIVERY_SHAPE_MIN_BENEFIT */
  /* Downstream-mirror table (rvtt-cost.md, carried through the one
     delivery-cost API -- FABLE_GOES_BURR #12; used ONLY to predict
     whether the replay-hoist gate lifts a record out of the loop):
     the mirror calls the SAME replay_pricing spelling the RTL gate
     prices with, so mirror drift is structurally impossible.  The
     defaults repeat the audited values for standalone checking.  */
  rvtt_delivery_cost::cost_table dcost = { 123, 100, 70, 300 };
  int ds_hoist_min_benefit = 60;
  bool hoist_enabled = false;	/* -mtt-tensix-optimize-replay-hoist */
  bool record_hoist_enabled = false; /* -mtt-tensix-optimize-replay-
					record-hoist: the downstream
					re-record gate prices the
					measurement model */
  bool completion_guard = false; /* -mtt-tensix-replay-hoist-
				    completion-guard: drain-inclusive
				    completion contract */
  bool autoincr_enabled = false; /* -mtt-tensix-optimize-dst-autoincr:
				    launch separators absorbed, so the
				    mirror's saturation run counts the
				    contiguous siblings */
  /* Once-per-entry Dst-auto-increment setup charge of a window shape
     (rvtt-cost.md XTT_AUTOINCR_SETUP_COST_X100 via the delivery-cost
     module; the pass's former W_drain MODEL SEAM.  Current-model
     value 0: the measured lane-EE table absorbs the SETC16 program in
     the once-per-group record delivery).  */
  int64_t autoincr_setup_x100 = 0;
};

enum class rvtt_delivery_mode
{
  rolled_explicit,	/* U = 1, per-trip RISC-pushed words */
  rolled_hoisted,	/* U = 1, record-once + one launch per trip */
  group_rerecord,	/* U >= 2, re-record + launches per group */
  group_hoisted,	/* U >= 2, record-once + launches per group */
  unrolled_explicit	/* U >= 2, straight-pushed copies: the row's
			   replay-safe span is below the former's
			   MIN_SEQUENCE (or above the buffer), so the
			   copies deliver explicitly and the request
			   buys only loop-control amortization -- the
			   measured tiny-row winning shape */
};

struct rvtt_delivery_candidate
{
  unsigned factor = 1;		/* U; 1 = rolled */
  unsigned payload_rows = 0;	/* R (window shapes only) */
  rvtt_delivery_mode mode = rvtt_delivery_mode::rolled_explicit;
  int64_t cost_blb = 0;		/* total modeled cs at boundary_lb */
  int64_t cost_bub = 0;		/* total modeled cs at boundary_ub */
};

struct rvtt_delivery_solution
{
  rvtt_solver_status status = rvtt_solver_status::unavailable;
  const char *diagnostic = "none";
  rvtt_delivery_candidate selected;
  /* The U = 1 reference (its predicted materialization).  */
  rvtt_delivery_candidate rolled;
  /* Benefit of the selected candidate versus the rolled reference,
     minimized over the boundary interval; the firing criterion.  */
  int64_t benefit_min = 0;
  /* True when no (U, R) window fits the former's minimum sequence and
     the capture-slot budget: the named window-budget condition.  */
  bool window_infeasible = false;
  unsigned solver_nodes = 0;
  /* Every enumerated candidate, for dump transparency.  */
  std::vector<rvtt_delivery_candidate> candidates;
};

/* Exact delivery-shape solver (rvtt-bnb.cc): deterministic exhaustive
   branch-and-bound over the candidate lattice with an admissible
   incumbent prune; always compiled; no external dependency.  */
extern rvtt_delivery_solution
rvtt_bnb_delivery_shape (const rvtt_delivery_problem &);

#endif /* GCC_RVTT_SCHEDULE_H */
