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

/* The exact solver behind -mtt-tensix-pressure-schedule-use-milp.
   It solves this model:

     - one operation per issue slot (a permutation of the region);
     - every dependence edge (def, use) issues def strictly earlier;
     - after-slot liveness: a value is live after slot S when it is available
       (live-in, or defined at or before S) and still wanted (used strictly
       after S, or live-out);
     - after every slot the live count is at most REGISTER_CAPACITY;
     - objective: the fewest operations displaced from their preferred
       (deterministic list scheduler) issue slots.

   The size limits (at most 24 operations and 32 values) keep the search
   tree small, and a deterministic node limit turns the remaining worst
   case into a "capped" result rather than a long compile.  A capped
   search returns no schedule.

   The solver only chooses the issue order before register allocation, so
   that a coloring within the register capacity exists; it does not assign
   registers.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "rvtt-schedule.h"
#include "rvtt-timing.h"

#include <algorithm>

namespace {

/* Node limit; generous for the problem sizes accepted.  */
constexpr unsigned bnb_node_limit = 100000;

/* Return true if PROBLEM exceeds the solver's size limits.  */
bool
over_scope_p (const rvtt_sched_problem &problem)
{
  return problem.operation_count > 24 || problem.values.size () > 32;
}

/* Return true if PROBLEM is within the size limits and well formed: at
   least two operations, a nonzero capacity, preferred slots forming a
   permutation, dependence endpoints in range and distinct, and every
   value's def and uses in range with no use at its own def.  */

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

/* Depth-first search state.    REMAINING holds, per value, the
   count of still-unissued wanting positions (uses plus one for live-out); a
   defined value becomes live when issued with a nonzero remaining count and
   dies when its count reaches zero.  */
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
   duplicate defs, which a well-formed problem never contains.  */
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

/* Set LIVE_AFTER to the live count after issuing OP now, and return true
   if it would exceed the capacity.  Reads that reach their final wanting position kill their
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

/* Issue OP at the next slot: reads reaching their final wanting
   position kill their values, a still-wanted definition is born, the
   successors' pending-pred counts drop, and OP is appended to the
   current order.  */

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

/* Undo issue (OP): restore the wanting counts, the successors'
   pending-pred counts and the order; LIVE_BEFORE is the live count
   the caller saved before issuing.  */

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

/* Return a lower bound on further displacements once slots before
   NEXT_SLOT are filled: every unissued operation whose preferred slot is
   already taken.  */
unsigned
bnb_search::forced_displacements (unsigned next_slot) const
{
  unsigned forced = 0;
  for (unsigned op = 0; op != m_count; ++op)
    if (!m_issued[op] && m_problem.preferred_slot[op] < next_slot)
      ++forced;
  return forced;
}

/* Fill SLOT and deeper by depth-first search over the ready
   operations: the operation preferring SLOT is tried first, then the
   others by ascending index.  Branches are pruned against the best
   schedule found so far using forced_displacements and the capacity
   check.  Improvements are stored in m_best_order and m_best_cost.  */

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

/* Solve the problem.  An oversized problem is reported as capped and a
   malformed one as invalid_model.  If the preferred schedule is claimed
   feasible, just verify it; otherwise run the full search.  A search that
   hits the node limit returns no order.  */

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
      /* The preferred schedule is claimed feasible: check its
	 dependences and capacity and return it.  */
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
      /* Do not return the best schedule of an incomplete search.  */
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

} /* anonymous namespace */

/* Solve PROBLEM (the model in the file header) exactly with one
   branch-and-bound search instance.  */

rvtt_solver_solution
rvtt_bnb_schedule (const rvtt_sched_problem &problem)
{
  bnb_search search (problem);
  return search.run ();
}

/* Return true; the built-in solver is always available.  */

bool
rvtt_solver_available ()
{
  return true;
}

/* Delivery-shape selection: choose the unroll factor U and replay payload
   R for a counted SFPU row loop with a known trip count.

   For each U the solver predicts which delivery shape later passes will
   produce:
     - the row's Dst-step words (TTINCRWC/TTDSTFACE) cannot be replayed, so
       replay can record only the rest of the row; windows of several rows
       are possible only for rows without such words;
     - if the recordable span is below the replay pass's MIN_SEQUENCE or
       above the 32-slot buffer, nothing is recorded and the unrolled
       copies are delivered explicitly;
     - the replay-hoist decision is predicted with the same rvtt_timing
       cost functions and constants that pass uses;
     - Dst auto-increment, when enabled, runs after replay formation and
       absorbs the separator words around launches.
   Each predicted shape is then priced with the delivery cost table, and
   the cheapest is chosen deterministically.  The search space is small
   enough to enumerate.

   The cost of a launch boundary is known only as an interval (1.3 to 1.8
   cycles, depending on how exposed the serial chain is), so every candidate
   is priced at both ends and an unrolled shape must clear the benefit
   threshold at both.  */

namespace {

/* Return the larger of A and B.  Delivery costs are int64_t because slot
   counts scaled by 100 can overflow 32 bits.  */

int64_t
imax64 (int64_t a, int64_t b)
{
  return a > b ? a : b;
}

/* Return the number of words of a row that replay can record.  */
unsigned
safe_words (const rvtt_delivery_problem &p)
{
  return p.row_words - p.barrier_words;
}

/* Return the execution slots per row of a replay window; separators
   absorbed by Dst auto-increment are neither executed nor delivered.  */
unsigned
window_exec (const rvtt_delivery_problem &p)
{
  return p.autoincr_enabled ? p.row_exec - p.barrier_words : p.row_exec;
}

/* Return the separator words delivered per row of a replay window.  */
unsigned
window_sep (const rvtt_delivery_problem &p)
{
  return p.autoincr_enabled ? 0 : p.barrier_words;
}

/* Predict whether the replay pass hoists the record of the counted loop
   out of the rolled row loop, using the same rvtt_timing cost function and
   the interlock execution estimate DS_EXEC.  */
bool
mirror_counted_hoist_fires (const rvtt_delivery_problem &p)
{
  if (!p.hoist_enabled
      || safe_words (p) < p.min_sequence
      || safe_words (p) > p.capture_slots)
    return false;
  rvtt_timing::hoist_costs costs;
  costs.push = p.dcost.push_x100;
  costs.slot = p.dcost.slot_x100;
  costs.turnaround = p.dcost.turnaround_x100;
  costs.record_overhead = p.dcost.record_overhead_x100;
  return rvtt_timing::counted_hoist_price (costs, p.trips, p.row_words,
					   p.ds_exec).benefit
	 >= p.ds_hoist_min_benefit;
}

/* Predict whether the replay pass hoists the re-record of an unrolled
   group body out of the group loop.  FACTOR is the unroll factor, GROUPS
   the trip count after unrolling and PAYLOAD_ROWS the rows per window.  */
bool
mirror_rerecord_hoist_fires (const rvtt_delivery_problem &p,
			     unsigned factor, unsigned payload_rows,
			     unsigned groups)
{
  if (!p.hoist_enabled || groups < 2)
    return false;
  /* No completion guard is assumed.  The launches form a contiguous run
     only when Dst auto-increment absorbs the separators.  */
  const unsigned payload_slots = payload_rows * safe_words (p);
  const unsigned run = p.autoincr_enabled ? factor / payload_rows : 1;
  rvtt_timing::hoist_costs costs;
  costs.push = p.dcost.push_x100;
  costs.slot = p.dcost.slot_x100;
  costs.turnaround = p.dcost.turnaround_x100;
  costs.record_overhead = p.dcost.record_overhead_x100;
  return rvtt_timing::rerecord_hoist_price
	   (costs, groups, payload_slots,
	    (int64_t) payload_rows * (p.ds_exec - p.barrier_words),
	    run, false).benefit
	 >= p.ds_hoist_min_benefit;
}

/* Return the cost of one explicit row without loop control (a peeled
   remainder copy).  */
int64_t
delivery_explicit_row (const rvtt_delivery_problem &p)
{
  return imax64 ((int64_t) p.row_exec * 100,
		 (int64_t) p.row_words * p.word);
}

/* Return the cost of the explicit loop without unrolling.  */
int64_t
delivery_rolled_explicit_cost (const rvtt_delivery_problem &p)
{
  return (int64_t) p.trips
	 * imax64 ((int64_t) p.row_exec * 100,
		   (int64_t) (p.row_words + p.control_words) * p.word);
}

/* Return the one-off Dst auto-increment setup cost of a window shape
   (XTT_AUTOINCR_SETUP_COST_X100, currently 0).  */
int64_t
autoincr_setup_term (const rvtt_delivery_problem &p)
{
  return p.autoincr_enabled ? p.autoincr_setup_x100 : 0;
}

/* Return the cost of the hoisted loop without unrolling (record once, one
   launch per trip) at boundary cost B: the record words, plus per trip the
   payload execution, one boundary and any remaining separator words.
   Loop-control delivery is hidden behind the execution backlog that a
   launch loop builds up.  */
int64_t
delivery_rolled_hoisted_cost (const rvtt_delivery_problem &p, int64_t b)
{
  return (int64_t) (1 + safe_words (p)) * p.word
	 + autoincr_setup_term (p)
	 + (int64_t) p.trips
	   * ((int64_t) window_exec (p) * 100 + b
	      + (int64_t) window_sep (p) * p.word);
}

/* Return the cost of unrolling by FACTOR with replay windows of
   PAYLOAD_ROWS rows, at boundary cost B.  HOISTED selects recording once
   rather than once per group.  Trips not covered by full groups are
   peeled by the generic unroller and priced as explicit rows, which is an
   upper bound since peeled copies may still be replayed.  */
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

/* Return the cost of unrolling by FACTOR when no replay window fits (the
   recordable span is below MIN_SEQUENCE or above the buffer size): the
   copies are delivered explicitly and only loop control is saved.  */
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

/* Return the payload R (rows per window) that the replay pass would
   choose when unrolling by FACTOR: the one maximizing its score
   (clones - 1) * (length - 1).  R > 1 is possible only when the row has
   no barrier word, since a window cannot span a typed Dst step.  Ties go
   to the smaller R.  Return 0 if no window fits.  */
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

/* Choose the delivery shape for problem P as described above.  Return
   every candidate, the rolled reference and the selection (the lowest cost
   at the upper boundary end, ties going to the smaller factor), with the
   benefit minimized over the boundary interval.  A malformed P gives
   invalid_model.  */

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

  /* The loop without unrolling, priced at both boundary ends.  */
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
  /* Enumerate dividing factors when any exists in range: for them the modeled
     shape is exact (whole groups, no peel).  Only when the trip count has no
     divisor in range (e.g. a prime trip count) are other factors tried,
     with the peeled remainder priced explicitly.  */
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

      /* Prune: every group must at least execute its rows and the
	 remainder must at least be delivered; if that lower bound already
	 reaches the best cost, this factor cannot win.  */
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

  /* Benefit over the rolled loop, minimized over the boundary interval.
     Costs are linear in B, so the two ends suffice.  */
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

/* Return the dump name of solver STATUS.  */

const char *
rvtt_solver_status_name (rvtt_solver_status status)
{
  switch (status)
    {
    case rvtt_solver_status::unavailable: return "unavailable";
    case rvtt_solver_status::capped: return "capped";
    case rvtt_solver_status::optimal: return "optimal";
    case rvtt_solver_status::infeasible: return "infeasible";
    case rvtt_solver_status::nonoptimal: return "nonoptimal";
    case rvtt_solver_status::invalid_model: return "invalid-model";
    case rvtt_solver_status::internal_error: return "internal-error";
    }
  gcc_unreachable ();
}

/* Solve PROBLEM with the built-in exact backend.  */

rvtt_solver_solution
rvtt_solve_schedule (const rvtt_sched_problem &problem)
{
  return rvtt_bnb_schedule (problem);
}
