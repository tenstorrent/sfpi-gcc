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

} // anonymous namespace

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
