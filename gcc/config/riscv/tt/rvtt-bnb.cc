/* Built-in exact branch-and-bound solver for Tensix SFPU scheduling.
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

/* This is the PRIMARY solver behind -mtt-tensix-pressure-schedule-use-milp.
   It solves exactly the model rvtt-lpsolve.cc formulates as a MILP:

     - one operation per issue slot (a permutation of the region);
     - every dependence edge (def, use) issues def strictly earlier;
     - after-slot liveness: a value is live after slot S when it is
       available (live-in, or defined at or before S) and still wanted
       (used strictly after S, or live-out);
     - after every slot the live count is at most REGISTER_CAPACITY;
     - objective: the fewest operations displaced from their preferred
       (deterministic list scheduler) issue slots.

   The scope caps carried over from the MILP (at most 24 operations and
   32 values) keep the search tree tiny, and a deterministic node cap
   turns the residual worst case into a "capped" verdict rather than a
   compile-time pause.  A capped search never contributes an incumbent,
   exactly like the lp_solve adapter's node-cap abort.

   Being dependency-free and always compiled, this solver is what a
   production build runs.  When GCC was configured --with-lp-solve, the
   lp_solve adapter is additionally run as an independent CROSS-CHECK
   (terminal statuses and objective values must agree); its answer is
   never selected, so code generation is byte-identical across build
   configurations.

   Relationship to register allocation: this solver (like the whole
   rvtt_lp_schedule pass) optimizes GIMPLE ISSUE ORDER under after-slot
   liveness -- a pre-allocation pressure rescue.  It claims no physical
   coloring: rtl-rvtt-lp-alloc.cc audits the pseudo liveness that
   actually reaches the allocator, and any future SFPU coloring
   allocator (e.g. a DSATUR-style assignment pass) colors the intervals
   this schedule produces.  If such an allocator lands, the division
   stays: coloring decides WHERE values live, this ILP decides WHEN
   values are live so that a capacity-respecting coloring exists.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "rvtt-schedule.h"
#include "rvtt-timing.h"

#include <algorithm>

namespace {

/* Mirror of rvtt-lpsolve.cc's node cap: deterministic, and generous
   for the capped problem sizes.  */
constexpr unsigned bnb_node_limit = 100000;

/* Scope and well-formedness envelope, deliberately identical to
   lpsolve_model::valid_problem so the two backends accept exactly the
   same problems.  */
bool
over_scope_p (const rvtt_sched_problem &problem)
{
  return problem.operation_count > 24 || problem.values.size () > 32;
}

bool
valid_problem_p (const rvtt_sched_problem &problem)
{
  const unsigned count = problem.operation_count;
  if (count < 2 || over_scope_p (problem)
      || problem.register_capacity == 0
      || problem.preferred_slot.size () != count)
    return false;

  std::vector<bool> seen_slot (count, false);
  for (unsigned slot : problem.preferred_slot)
    if (slot >= count || seen_slot[slot])
      return false;
    else
      seen_slot[slot] = true;

  for (const auto &edge : problem.dependencies)
    if (edge.first >= count || edge.second >= count
	|| edge.first == edge.second)
      return false;

  for (const rvtt_sched_value &value : problem.values)
    {
      if (value.def < -1 || value.def >= static_cast<int> (count))
	return false;
      for (unsigned use : value.uses)
	if (use >= count || static_cast<int> (use) == value.def)
	  return false;
    }
  return true;
}

/* Exact after-slot liveness search state.  REMAINING holds, per value,
   the count of still-unissued wanting positions (uses plus one for
   live-out); a defined value becomes live when issued with a nonzero
   remaining count and dies when its count reaches zero, which is the
   same arithmetic the pass's pressure_for_order uses.  */
class bnb_search
{
public:
  explicit bnb_search (const rvtt_sched_problem &problem)
    : m_problem (problem), m_count (problem.operation_count)
  {}

  rvtt_solver_solution run ();

private:
  bool prepare (rvtt_solver_solution &solution);
  bool issue_would_overflow (unsigned op, unsigned &live_after) const;
  void issue (unsigned op);
  void retract (unsigned op, unsigned live_before);
  void dfs (unsigned slot);
  unsigned forced_displacements (unsigned next_slot) const;

  const rvtt_sched_problem &m_problem;
  const unsigned m_count;

  /* Per-op: dependence bookkeeping and value effects.  */
  std::vector<unsigned> m_pending_preds;
  std::vector<std::vector<unsigned>> m_successors;
  std::vector<std::vector<unsigned>> m_op_reads;  /* value ids read */
  std::vector<int> m_op_defines;		  /* value id or -1 */

  std::vector<unsigned> m_remaining;	/* per value */
  std::vector<bool> m_issued;
  std::vector<unsigned> m_order;	/* op at each filled slot */

  unsigned m_live = 0;
  unsigned m_cost = 0;
  unsigned m_nodes = 0;
  bool m_capped = false;

  unsigned m_best_cost = 0;
  bool m_have_best = false;
  std::vector<unsigned> m_best_order;
};

/* Translate the problem into per-op read/define lists.  Fails only on
   duplicate defs, which the GIMPLE-side builder never produces.  */
bool
bnb_search::prepare (rvtt_solver_solution &solution)
{
  const unsigned value_count = m_problem.values.size ();

  m_pending_preds.assign (m_count, 0);
  m_successors.assign (m_count, {});
  for (const auto &edge : m_problem.dependencies)
    {
      /* Collapse duplicate edges so pred counts stay balanced.  */
      if (std::find (m_successors[edge.first].begin (),
		     m_successors[edge.first].end (), edge.second)
	  != m_successors[edge.first].end ())
	continue;
      m_successors[edge.first].push_back (edge.second);
      ++m_pending_preds[edge.second];
    }

  m_op_reads.assign (m_count, {});
  m_op_defines.assign (m_count, -1);
  m_remaining.assign (value_count, 0);
  m_live = 0;
  for (unsigned value_id = 0; value_id != value_count; ++value_id)
    {
      const rvtt_sched_value &value = m_problem.values[value_id];
      m_remaining[value_id]
	= value.uses.size () + (value.live_out ? 1 : 0);
      if (value.def < 0)
	++m_live;
      else
	{
	  if (m_op_defines[value.def] >= 0)
	    {
	      solution.status = rvtt_solver_status::invalid_model;
	      solution.diagnostic = "duplicate-def";
	      return false;
	    }
	  m_op_defines[value.def] = value_id;
	}
      for (unsigned use : value.uses)
	m_op_reads[use].push_back (value_id);
    }

  if (m_live > m_problem.register_capacity)
    {
      solution.status = rvtt_solver_status::infeasible;
      solution.diagnostic = "live-in";
      return false;
    }

  m_issued.assign (m_count, false);
  m_order.clear ();
  m_order.reserve (m_count);
  m_cost = 0;
  m_nodes = 0;
  m_capped = false;
  m_have_best = false;
  m_best_cost = 0;
  m_best_order.clear ();
  return true;
}

/* After-slot live count if OP issued now; true when it would exceed
   capacity.  Reads that reach their final wanting position kill their
   value; a defined value with remaining wanting positions is born.  */
bool
bnb_search::issue_would_overflow (unsigned op, unsigned &live_after) const
{
  unsigned deaths = 0;
  for (unsigned value_id : m_op_reads[op])
    if (m_remaining[value_id] == 1)
      ++deaths;
  unsigned births = 0;
  const int defined = m_op_defines[op];
  if (defined >= 0 && m_remaining[defined] != 0)
    births = 1;
  live_after = m_live - deaths + births;
  return live_after > m_problem.register_capacity;
}

void
bnb_search::issue (unsigned op)
{
  for (unsigned value_id : m_op_reads[op])
    if (--m_remaining[value_id] == 0)
      --m_live;
  const int defined = m_op_defines[op];
  if (defined >= 0 && m_remaining[defined] != 0)
    ++m_live;
  m_issued[op] = true;
  for (unsigned successor : m_successors[op])
    --m_pending_preds[successor];
  m_order.push_back (op);
}

void
bnb_search::retract (unsigned op, unsigned live_before)
{
  m_order.pop_back ();
  for (unsigned successor : m_successors[op])
    ++m_pending_preds[successor];
  m_issued[op] = false;
  for (unsigned value_id : m_op_reads[op])
    ++m_remaining[value_id];
  m_live = live_before;
}

/* Every unissued operation whose preferred slot is already filled can
   no longer land on it: an admissible displacement lower bound.  */
unsigned
bnb_search::forced_displacements (unsigned next_slot) const
{
  unsigned forced = 0;
  for (unsigned op = 0; op != m_count; ++op)
    if (!m_issued[op] && m_problem.preferred_slot[op] < next_slot)
      ++forced;
  return forced;
}

void
bnb_search::dfs (unsigned slot)
{
  if (m_capped)
    return;
  if (slot == m_count)
    {
      if (!m_have_best || m_cost < m_best_cost)
	{
	  m_have_best = true;
	  m_best_cost = m_cost;
	  m_best_order = m_order;
	}
      return;
    }

  if (m_have_best
      && m_cost + forced_displacements (slot) >= m_best_cost)
    return;

  /* Deterministic branch order: the operation that keeps this slot
     undisplaced first, then ascending operation index.  */
  for (int pass = 0; pass != 2 && !m_capped; ++pass)
    for (unsigned op = 0; op != m_count && !m_capped; ++op)
      {
	const bool preferred_here = m_problem.preferred_slot[op] == slot;
	if ((pass == 0) != preferred_here)
	  continue;
	if (m_issued[op] || m_pending_preds[op] != 0)
	  continue;

	const unsigned displaced = preferred_here ? 0 : 1;
	if (m_have_best && m_cost + displaced >= m_best_cost)
	  continue;

	unsigned live_after;
	if (issue_would_overflow (op, live_after))
	  continue;

	if (++m_nodes > bnb_node_limit)
	  {
	    m_capped = true;
	    return;
	  }

	const unsigned live_before = m_live;
	issue (op);
	m_cost += displaced;
	dfs (slot + 1);
	m_cost -= displaced;
	retract (op, live_before);
      }
}

rvtt_solver_solution
bnb_search::run ()
{
  rvtt_solver_solution solution;

  if (!valid_problem_p (m_problem))
    {
      solution.status = (over_scope_p (m_problem)
			 ? rvtt_solver_status::capped
			 : rvtt_solver_status::invalid_model);
      solution.diagnostic = "problem";
      return solution;
    }
  if (!prepare (solution))
    return solution;

  if (m_problem.preferred_feasible)
    {
      /* The preferred schedule is asserted feasible: verify it exactly
	 (dependences and capacity) and return it, mirroring the MILP's
	 fixed-bounds certificate solve.  */
      std::vector<unsigned> op_at (m_count);
      for (unsigned op = 0; op != m_count; ++op)
	op_at[m_problem.preferred_slot[op]] = op;
      for (unsigned slot = 0; slot != m_count; ++slot)
	{
	  const unsigned op = op_at[slot];
	  if (m_pending_preds[op] != 0)
	    {
	      solution.status = rvtt_solver_status::infeasible;
	      solution.diagnostic = "preferred-deps";
	      return solution;
	    }
	  unsigned live_after;
	  if (issue_would_overflow (op, live_after))
	    {
	      solution.status = rvtt_solver_status::infeasible;
	      solution.diagnostic = "preferred-capacity";
	      return solution;
	    }
	  issue (op);
	  ++m_nodes;
	}
      solution.order = m_order;
      solution.solver_nodes = m_nodes;
      solution.status = rvtt_solver_status::optimal;
      solution.diagnostic = "ok";
      return solution;
    }

  dfs (0);
  solution.solver_nodes = m_nodes;
  if (m_capped)
    {
      /* A capped search never contributes its incumbent.  */
      solution.status = rvtt_solver_status::capped;
      solution.diagnostic = "node-limit";
      return solution;
    }
  if (!m_have_best)
    {
      solution.status = rvtt_solver_status::infeasible;
      solution.diagnostic = "search";
      return solution;
    }
  solution.order = std::move (m_best_order);
  solution.status = rvtt_solver_status::optimal;
  solution.diagnostic = "ok";
  return solution;
}

/* Objective value of a solved order: operations away from their
   preferred slots.  Used only to cross-check backends.  */
unsigned
displacement_cost (const rvtt_sched_problem &problem,
		   const std::vector<unsigned> &order)
{
  unsigned cost = 0;
  for (unsigned slot = 0; slot != order.size (); ++slot)
    if (order[slot] < problem.preferred_slot.size ()
	&& problem.preferred_slot[order[slot]] != slot)
      ++cost;
  return cost;
}

} /* anonymous namespace */

rvtt_solver_solution
rvtt_bnb_schedule (const rvtt_sched_problem &problem)
{
  bnb_search search (problem);
  return search.run ();
}

bool
rvtt_solver_available ()
{
  /* The built-in exact solver is always compiled in.  */
  return true;
}

const char *
rvtt_solver_backend_name ()
{
  return rvtt_lpsolve_available () ? "bnb+lpsolve-check" : "bnb";
}

/* ---------------------------------------------------------------------
   Delivery-shape arbitration (lane EG): exact minimization over the
   discrete shape lattice {U} x {payload R} of one proven-trip counted
   SFPU row loop.

   For every unroll factor U the solver first PREDICTS which delivery
   shape the downstream machinery materializes:
     - the row's typed Dst-step words (TTINCRWC/TTDSTFACE) are
       xtt_replay=barrier, so the always-on replay former can only
       capture the row's replay-SAFE span (safe = row_words -
       barrier_words); multi-row payloads exist only for rows with no
       barrier word at all;
     - a safe span below the former's MIN_SEQUENCE (or above the
       32-slot buffer) captures nothing: the unrolled copies deliver
       explicitly (unrolled-explicit -- verified by compilation: eight
       copies, zero TTREPLAY);
     - the replay-hoist gate of rtl-rvtt-replay.cc may lift the record
       out of the loop; its decision is predicted by mirroring its own
       published rvtt-cost.md model with the DOWNSTREAM constants and
       ITS interlock exec estimate (ds_exec) -- prediction, never
       re-pricing (the mirror reproduces the recorded pin-13 refusal
       arithmetic exactly);
     - the Dst auto-increment pass runs after replay formation and
       absorbs the separator words around launches, so on replay legs
       under -mtt-tensix-optimize-dst-autoincr the separators neither
       execute nor deliver (the measured lane-EE log-fresh fact).
   It then prices the predicted shape with the MEASURED lane-EE
   delivery table (rvtt-schedule.h, rvtt-cost.md section) and returns
   the exact argmin, depth-first over the tiny lattice with an
   admissible incumbent prune and a deterministic order -- the same
   branch-and-bound discipline as the scheduling solver above.  A node
   cap mirrors the scheduling solver's for form; the lattice cannot
   approach it.

   The per-launch BOUNDARY cost is a measured INTERVAL (1.3..1.8
   cycles, serial-chain exposure); every candidate is priced at both
   ends and a non-rolled request must clear the benefit threshold at
   both.  */

namespace {

int64_t
imax64 (int64_t a, int64_t b)
{
  return a > b ? a : b;
}

/* The row's replay-safe span (slots the former can record).  */
unsigned
safe_words (const rvtt_delivery_problem &p)
{
  return p.row_words - p.barrier_words;
}

/* Window-leg execution slots per row: separators absorbed by the Dst
   auto-increment pass neither execute nor deliver.  */
unsigned
window_exec (const rvtt_delivery_problem &p)
{
  return p.autoincr_enabled ? p.row_exec - p.barrier_words : p.row_exec;
}

/* Window-leg separator words delivered per row.  */
unsigned
window_sep (const rvtt_delivery_problem &p)
{
  return p.autoincr_enabled ? 0 : p.barrier_words;
}

/* Downstream-mirror: does the replay-hoist gate lift the counted-loop
   record of a ROLLED row loop (rvtt-cost.md counted-loop capture
   branch, the gate's own interlock exec estimate)?  Since item #12 the
   mirror calls the SAME replay_pricing spelling the RTL gate prices
   with (rvtt-delivery-cost-core.h) -- prediction, never re-pricing,
   and drift is structurally impossible.  Validated: at trips 31,
   words 9, ds_exec 10 this prices the recorded pin-13 hardshrink
   refusal -383 exactly (pinned in rvtt-delivery-cost-test.cc).  */
bool
mirror_counted_hoist_fires (const rvtt_delivery_problem &p)
{
  if (!p.hoist_enabled
      || safe_words (p) < p.min_sequence
      || safe_words (p) > p.capture_slots)
    return false;
  const rvtt_delivery_cost::replay_price price
    = rvtt_delivery_cost::replay_pricing
	(p.dcost, rvtt_delivery_cost::SHAPE_COUNTED, p.trips, p.row_words,
	 p.ds_exec, /*launch_run=*/1, /*drain_contract=*/false,
	 p.ds_hoist_min_benefit);
  return price.profitable;
}

/* Downstream-mirror: does the replay-hoist gate lift the re-record
   pass of an UNROLLED group body (rvtt-cost.md re-record branches,
   including the execution-saturation context term) out of the group
   loop?  GROUPS is the post-unroll trip count, PAYLOAD_ROWS the
   former-ranked payload R.  The shape selection is the gate's own
   flag-pair spelling (rerecord_shape): under
   -mtt-tensix-optimize-replay-record-hoist the gate prices the
   measurement model and the mirror now predicts the same (formerly
   the delivery-shape MODEL SEAM: the mirror modeled only the pre-EC
   hoist machinery); at default flags the arithmetic is the pre-#12
   mirror's, bit for bit.  */
bool
mirror_rerecord_hoist_fires (const rvtt_delivery_problem &p,
			     unsigned factor, unsigned payload_rows,
			     unsigned groups)
{
  if (!p.hoist_enabled || groups < 2)
    return false;
  const unsigned payload_slots = payload_rows * safe_words (p);
  const int64_t exec_slots
    = (int64_t) payload_rows * (p.ds_exec - p.barrier_words);
  /* The launch run is contiguous only when the Dst auto-increment
     pass absorbs the typed separators.  */
  const unsigned run = p.autoincr_enabled ? factor / payload_rows : 1;
  const rvtt_delivery_cost::replay_shape shape
    = rvtt_delivery_cost::rerecord_shape (p.record_hoist_enabled,
					  p.completion_guard,
					  /*trips_proven=*/true);
  const rvtt_delivery_cost::replay_price price
    = rvtt_delivery_cost::replay_pricing
	(p.dcost, shape, groups, payload_slots, exec_slots, run,
	 p.completion_guard, p.ds_hoist_min_benefit);
  return price.profitable;
}

/* Measured-table price of one explicit row without loop control (a
   peeled remainder copy).  */
int64_t
delivery_explicit_row (const rvtt_delivery_problem &p)
{
  return imax64 ((int64_t) p.row_exec * 100,
		 (int64_t) p.row_words * p.word);
}

/* Measured-table price of the U = 1 explicit rolled loop.  */
int64_t
delivery_rolled_explicit_cost (const rvtt_delivery_problem &p)
{
  return (int64_t) p.trips
	 * imax64 ((int64_t) p.row_exec * 100,
		   (int64_t) (p.row_words + p.control_words) * p.word);
}

/* Once-per-entry Dst-auto-increment setup charge of a window shape
   (the pass's former W_drain MODEL SEAM, now the delivery-cost
   module's named quantity carried in the problem; current-model
   value 0 keeps every priced total unchanged, and a future
   silicon-priced value moves every consumer together).  */
int64_t
autoincr_setup_term (const rvtt_delivery_problem &p)
{
  return p.autoincr_enabled ? p.autoincr_setup_x100 : 0;
}

/* Measured-table price of the U = 1 hoisted shape (record once, one
   launch per trip) at boundary cost B.  EE closure form: payload
   slots at 1.0 + record words + one exposed boundary per launch +
   any surviving separator word; loop-control delivery hides under
   the execution backlog a launch loop necessarily accumulates (the
   ceil/log/rsqrt closures carry no control term).  */
int64_t
delivery_rolled_hoisted_cost (const rvtt_delivery_problem &p, int64_t b)
{
  return (int64_t) (1 + safe_words (p)) * p.word
	 + autoincr_setup_term (p)
	 + (int64_t) p.trips
	   * ((int64_t) window_exec (p) * 100 + b
	      + (int64_t) window_sep (p) * p.word);
}

/* Measured-table price of an unrolled group WINDOW shape at boundary
   cost B.  HOISTED selects record-once versus re-record per group.
   Trips not covered by full groups are peeled by the generic unroller
   and priced at explicit delivery -- an upper bound (peeled copies
   adjacent to the unrolled body can still join the former's clone
   sets).  */
int64_t
delivery_group_cost (const rvtt_delivery_problem &p, unsigned factor,
		     unsigned payload_rows, bool hoisted, int64_t b)
{
  const unsigned groups = p.trips / factor;
  const unsigned remainder = p.trips % factor;
  const unsigned payload_slots = payload_rows * safe_words (p);
  const int64_t record_words = (int64_t) (1 + payload_slots) * p.word;
  const int64_t group_exec
    = (int64_t) factor * window_exec (p) * 100;
  const int64_t group_sep
    = (int64_t) factor * window_sep (p) * p.word;
  const unsigned launches
    = factor / payload_rows - (hoisted ? 0 : 1);
  int64_t per_group = group_exec + group_sep + (int64_t) launches * b;
  int64_t once = autoincr_setup_term (p);
  if (hoisted)
    once += record_words;
  else
    per_group += record_words;
  return once + (int64_t) groups * per_group
	 + (int64_t) remainder * delivery_explicit_row (p);
}

/* Measured-table price of an unrolled group with NO capture (the
   former's safe span is below MIN_SEQUENCE or above the buffer): the
   copies deliver explicitly; the request buys loop-control
   amortization only.  Boundary-independent.  */
int64_t
delivery_unrolled_explicit_cost (const rvtt_delivery_problem &p,
				 unsigned factor)
{
  const unsigned groups = p.trips / factor;
  const unsigned remainder = p.trips % factor;
  const int64_t per_group
    = imax64 ((int64_t) factor * p.row_exec * 100,
	      ((int64_t) factor * p.row_words + p.control_words)
	      * p.word);
  return (int64_t) groups * per_group
	 + (int64_t) remainder * delivery_explicit_row (p);
}

/* The former-ranked payload for FACTOR -- argmax of the former's own
   clone-saving score (clones - 1) * (length - 1) over admissible
   payloads: R = 1 always a candidate when the safe span fits; R > 1
   only when the row carries no barrier word (a window cannot span the
   typed Dst step).  Ties to the smaller R (more clones).  0 when no
   window fits.  */
unsigned
delivery_payload_for (const rvtt_delivery_problem &p, unsigned factor)
{
  const unsigned safe = safe_words (p);
  const unsigned r_max = p.barrier_words ? 1 : factor;
  unsigned best = 0;
  int64_t best_score = -1;
  for (unsigned r = 1; r <= r_max; ++r)
    {
      if (factor % r != 0 || factor / r < 2)
	continue;
      const unsigned len = r * safe;
      if (len < p.min_sequence || len > p.capture_slots)
	continue;
      const int64_t score
	= (int64_t) (factor / r - 1) * ((int64_t) len - 1);
      if (score > best_score)
	{
	  best_score = score;
	  best = r;
	}
    }
  return best;
}

} /* anonymous namespace */

rvtt_delivery_solution
rvtt_bnb_delivery_shape (const rvtt_delivery_problem &p)
{
  rvtt_delivery_solution sol;

  if (p.trips < 2 || p.row_words == 0 || p.row_exec < p.row_words
      || p.barrier_words > p.row_words || p.ds_exec < p.row_exec
      || p.word == 0 || p.boundary_lb > p.boundary_ub
      || p.max_factor < 2 || p.control_words == 0)
    {
      sol.status = rvtt_solver_status::invalid_model;
      sol.diagnostic = "problem";
      return sol;
    }

  /* The U = 1 reference: predicted materialization, priced at both
     boundary ends.  */
  rvtt_delivery_candidate rolled;
  rolled.factor = 1;
  rolled.payload_rows = 0;
  if (mirror_counted_hoist_fires (p))
    {
      rolled.mode = rvtt_delivery_mode::rolled_hoisted;
      rolled.payload_rows = 1;
      rolled.cost_blb = delivery_rolled_hoisted_cost (p, p.boundary_lb);
      rolled.cost_bub = delivery_rolled_hoisted_cost (p, p.boundary_ub);
    }
  else
    {
      rolled.mode = rvtt_delivery_mode::rolled_explicit;
      rolled.cost_blb = delivery_rolled_explicit_cost (p);
      rolled.cost_bub = rolled.cost_blb;
    }
  sol.rolled = rolled;
  sol.candidates.push_back (rolled);
  sol.selected = rolled;
  ++sol.solver_nodes;

  bool any_window = false;
  const unsigned factor_cap
    = p.max_factor < p.trips ? p.max_factor : p.trips;
  /* Enumerate dividing factors when any exists in range: for them the
     modeled shape is exact (whole groups, no peel).  Only when the
     trip count has no divisor in range (prime trips -- the production
     31-row loops) are non-dividing factors admitted, with the peeled
     remainder priced explicitly (documented approximation: peeled
     copies can still join the former's clone sets).  */
  bool has_divisor = false;
  for (unsigned factor = 2; factor <= factor_cap; ++factor)
    if (p.trips % factor == 0)
      {
	has_divisor = true;
	break;
      }
  for (unsigned factor = 2; factor <= factor_cap; ++factor)
    {
      if (has_divisor && p.trips % factor != 0)
	continue;
      if (++sol.solver_nodes > bnb_node_limit)
	{
	  sol.status = rvtt_solver_status::capped;
	  sol.diagnostic = "node-limit";
	  return sol;
	}
      /* Code-size budget: total straight-line row words per group.  */
      if ((int64_t) factor * p.row_words > p.max_words)
	continue;

      /* Admissible prune: every group must at least execute its rows
	 and the remainder must at least deliver -- if that floor
	 already meets the incumbent, no leg of this factor can win.  */
      const int64_t optimistic
	= (int64_t) (p.trips / factor)
	  * (int64_t) factor * window_exec (p) * 100
	  + (int64_t) (p.trips % factor) * delivery_explicit_row (p);
      if (optimistic >= sol.selected.cost_bub)
	continue;

      rvtt_delivery_candidate cand;
      cand.factor = factor;
      const unsigned payload_rows = delivery_payload_for (p, factor);
      if (payload_rows == 0)
	{
	  /* No window fits: the copies deliver explicitly.  */
	  cand.payload_rows = 0;
	  cand.mode = rvtt_delivery_mode::unrolled_explicit;
	  cand.cost_blb = delivery_unrolled_explicit_cost (p, factor);
	  cand.cost_bub = cand.cost_blb;
	}
      else
	{
	  any_window = true;
	  const unsigned groups = p.trips / factor;
	  const bool hoisted
	    = mirror_rerecord_hoist_fires (p, factor, payload_rows,
					   groups);
	  cand.payload_rows = payload_rows;
	  cand.mode = hoisted ? rvtt_delivery_mode::group_hoisted
			      : rvtt_delivery_mode::group_rerecord;
	  cand.cost_blb
	    = delivery_group_cost (p, factor, payload_rows, hoisted,
				   p.boundary_lb);
	  cand.cost_bub
	    = delivery_group_cost (p, factor, payload_rows, hoisted,
				   p.boundary_ub);
	}
      sol.candidates.push_back (cand);

      /* Deterministic selection: strictly better at the conservative
	 boundary end wins; ties keep the smaller factor.  */
      if (cand.cost_bub < sol.selected.cost_bub)
	sol.selected = cand;
    }
  sol.window_infeasible = !any_window;

  /* Firing benefit versus the rolled reference, minimized over the
     boundary interval (each leg is linear in B between the priced
     ends, so the two ends bound the interval exactly).  */
  if (sol.selected.factor >= 2)
    {
      const int64_t b_lb = sol.rolled.cost_blb - sol.selected.cost_blb;
      const int64_t b_ub = sol.rolled.cost_bub - sol.selected.cost_bub;
      sol.benefit_min = b_lb < b_ub ? b_lb : b_ub;
    }

  sol.status = rvtt_solver_status::optimal;
  sol.diagnostic = "ok";
  return sol;
}

rvtt_solver_solution
rvtt_solve_schedule (const rvtt_sched_problem &problem)
{
  rvtt_solver_solution solution = rvtt_bnb_schedule (problem);
  if (!rvtt_lpsolve_available ())
    return solution;

  /* Cross-check terminal verdicts against the optional lp_solve
     backend.  Its answer is never selected: only agreement is
     recorded, so configuring --with-lp-solve cannot change code
     generation.  */
  rvtt_solver_solution check = rvtt_lpsolve_schedule (problem);
  const auto terminal_p = [] (const rvtt_solver_solution &s)
    {
      return s.status == rvtt_solver_status::optimal
	|| s.status == rvtt_solver_status::infeasible;
    };
  if (!terminal_p (solution) || !terminal_p (check))
    solution.cross_check = "skipped";
  else if (solution.status != check.status)
    solution.cross_check = "disagree";
  else if (solution.status == rvtt_solver_status::optimal
	   && (displacement_cost (problem, solution.order)
	       != displacement_cost (problem, check.order)))
    solution.cross_check = "disagree";
  else
    solution.cross_check = "agree";
  return solution;
}
