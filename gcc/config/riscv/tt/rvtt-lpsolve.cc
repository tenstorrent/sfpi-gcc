/* Optional lp_solve adapter for Tensix SFPU scheduling.
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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "rvtt-schedule.h"

#ifdef HAVE_LPSOLVE
#include <lpsolve/lp_lib.h>
#endif

#include <algorithm>
#include <limits>
#include <unordered_set>

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

bool
rvtt_lpsolve_available ()
{
#ifdef HAVE_LPSOLVE
  return true;
#else
  return false;
#endif
}

#ifndef HAVE_LPSOLVE

rvtt_solver_solution
rvtt_lpsolve_schedule (const rvtt_sched_problem &)
{
  return {};
}

#else

namespace {

/* The first MILP is deliberately schedule-only.  It uses one issue slot per
   operation and exact after-slot liveness.  Reading dying operands and writing
   their replacement in the same slot is therefore representable without
   claiming a physical coloring that GIMPLE cannot enforce.  */
class lpsolve_model
{
public:
  explicit lpsolve_model (const rvtt_sched_problem &problem)
    : m_problem (problem), m_count (problem.operation_count),
      m_value_count (problem.values.size ()),
      m_columns (m_count * m_count + 2 * m_value_count * m_count),
      m_lp (make_lp (0, m_columns))
  {}

  ~lpsolve_model ()
  {
    if (m_lp)
      delete_lp (m_lp);
  }

  rvtt_solver_solution solve ();

private:
  static constexpr COUNTER node_limit = 100000;

  static int __WINAPI node_limit_abort (lprec *lp, void *)
  {
    return get_total_nodes (lp) >= node_limit;
  }

  int x (unsigned op, unsigned slot) const
  {
    return 1 + op * m_count + slot;
  }

  int live (unsigned value, unsigned slot) const
  {
    return 1 + m_count * m_count + value * m_count + slot;
  }

  int used_after (unsigned value, unsigned slot) const
  {
    return 1 + m_count * m_count + m_value_count * m_count
      + value * m_count + slot;
  }

  bool add_row (const std::vector<int> &, const std::vector<REAL> &,
		int, REAL);
  bool valid_problem () const;
  bool build ();

  const rvtt_sched_problem &m_problem;
  const unsigned m_count;
  const unsigned m_value_count;
  const int m_columns;
  lprec *m_lp;
};

bool
lpsolve_model::add_row (const std::vector<int> &columns,
			const std::vector<REAL> &coefficients,
			int kind, REAL rhs)
{
  gcc_assert (columns.size () == coefficients.size ());
  if (columns.empty ())
    return (kind == LE ? 0 <= rhs : kind == GE ? 0 >= rhs : rhs == 0);
  return add_constraintex (m_lp, columns.size (),
			   const_cast<REAL *> (coefficients.data ()),
			   const_cast<int *> (columns.data ()), kind, rhs);
}

bool
lpsolve_model::valid_problem () const
{
  if (m_count < 2 || m_count > 24 || m_value_count > 32
      || m_problem.register_capacity == 0
      || m_problem.preferred_slot.size () != m_count
      || m_columns <= 0)
    return false;

  std::vector<bool> seen_slot (m_count, false);
  for (unsigned slot : m_problem.preferred_slot)
    if (slot >= m_count || seen_slot[slot])
      return false;
    else
      seen_slot[slot] = true;

  for (const auto &edge : m_problem.dependencies)
    if (edge.first >= m_count || edge.second >= m_count
	|| edge.first == edge.second)
      return false;

  for (const rvtt_sched_value &value : m_problem.values)
    {
      if (value.def < -1 || value.def >= static_cast<int> (m_count))
	return false;
      for (unsigned use : value.uses)
	if (use >= m_count || static_cast<int> (use) == value.def)
	  return false;
    }
  return true;
}

bool
lpsolve_model::build ()
{
  if (!m_lp || !valid_problem ())
    return false;

  set_verbose (m_lp, CRITICAL);
  set_minim (m_lp);
  /* A deterministic branch-and-bound node cap avoids a pathological small
     region turning into an unbounded compiler pause.  A capped solve is never
     allowed to contribute its incumbent.  */
  put_abortfunc (m_lp, node_limit_abort, this);
  set_add_rowmode (m_lp, TRUE);

  for (unsigned op = 0; op != m_count; ++op)
    for (unsigned slot = 0; slot != m_count; ++slot)
      if (m_problem.preferred_feasible)
	{
	  const REAL fixed = slot == m_problem.preferred_slot[op] ? 1 : 0;
	  if (!set_bounds (m_lp, x (op, slot), fixed, fixed))
	    return false;
	}
      else if (!set_binary (m_lp, x (op, slot), TRUE))
	return false;
  /* LIVE and USED_AFTER are exact linearizations of binary issue choices;
     bounds are sufficient and avoid hundreds of unnecessary MIP columns.  */
  for (int column = 1 + m_count * m_count; column <= m_columns; ++column)
    if (!set_bounds (m_lp, column, 0, 1))
      return false;

  std::vector<int> columns;
  std::vector<REAL> coefficients;

  /* Every operation has exactly one issue slot.  */
  for (unsigned op = 0; op != m_count; ++op)
    {
      columns.clear ();
      coefficients.clear ();
      for (unsigned slot = 0; slot != m_count; ++slot)
	{
	  columns.push_back (x (op, slot));
	  coefficients.push_back (1);
	}
      if (!add_row (columns, coefficients, EQ, 1))
	return false;
    }

  /* The first model is single-issue, so every slot contains one operation.  */
  for (unsigned slot = 0; slot != m_count; ++slot)
    {
      columns.clear ();
      coefficients.clear ();
      for (unsigned op = 0; op != m_count; ++op)
	{
	  columns.push_back (x (op, slot));
	  coefficients.push_back (1);
	}
      if (!add_row (columns, coefficients, EQ, 1))
	return false;
    }

  /* issue(def) + 1 <= issue(use).  */
  for (const auto &edge : m_problem.dependencies)
    {
      columns.clear ();
      coefficients.clear ();
      for (unsigned slot = 0; slot != m_count; ++slot)
	{
	  columns.push_back (x (edge.first, slot));
	  coefficients.push_back (slot);
	  columns.push_back (x (edge.second, slot));
	  coefficients.push_back (-static_cast<REAL> (slot));
	}
      if (!add_row (columns, coefficients, LE, -1))
	return false;
    }

  unsigned live_in = 0;
  for (unsigned value_id = 0; value_id != m_value_count; ++value_id)
    {
      const rvtt_sched_value &value = m_problem.values[value_id];
      if (value.def < 0)
	++live_in;

      for (unsigned slot = 0; slot != m_count; ++slot)
	{
	  const int live_column = live (value_id, slot);

	  if (value.live_out)
	    {
	      if (value.def < 0)
		{
		  columns = { live_column };
		  coefficients = { 1 };
		  if (!add_row (columns, coefficients, EQ, 1))
		    return false;
		}
	      else
		{
		  columns = { live_column };
		  coefficients = { 1 };
		  for (unsigned def_slot = 0; def_slot <= slot; ++def_slot)
		    {
		      columns.push_back (x (value.def, def_slot));
		      coefficients.push_back (-1);
		    }
		  if (!add_row (columns, coefficients, EQ, 0))
		    return false;
		}
	      continue;
	    }

	  /* USED_AFTER is the exact OR of every use scheduled later than
	     SLOT.  Factoring this term avoids a cubic def/use-slot product.  */
	  const int after_column = used_after (value_id, slot);
	  columns = { after_column };
	  coefficients = { 1 };
	  for (unsigned use : value.uses)
	    for (unsigned use_slot = slot + 1; use_slot != m_count;
		 ++use_slot)
	      {
		columns.push_back (x (use, use_slot));
		coefficients.push_back (-1);
	      }
	  if (!add_row (columns, coefficients, LE, 0))
	    return false;
	  for (unsigned use : value.uses)
	    for (unsigned use_slot = slot + 1; use_slot != m_count;
		 ++use_slot)
	      {
		columns = { after_column, x (use, use_slot) };
		coefficients = { 1, -1 };
		if (!add_row (columns, coefficients, GE, 0))
		  return false;
	      }

	  if (value.def < 0)
	    {
	      columns = { live_column, after_column };
	      coefficients = { 1, -1 };
	      if (!add_row (columns, coefficients, EQ, 0))
		return false;
	    }
	  else
	    {
	      /* LIVE = DEFINED_BY_SLOT AND USED_AFTER.  */
	      columns = { live_column, after_column };
	      coefficients = { 1, -1 };
	      if (!add_row (columns, coefficients, LE, 0))
		return false;

	      columns = { live_column };
	      coefficients = { 1 };
	      for (unsigned def_slot = 0; def_slot <= slot; ++def_slot)
		{
		  columns.push_back (x (value.def, def_slot));
		  coefficients.push_back (-1);
		}
	      if (!add_row (columns, coefficients, LE, 0))
		return false;

	      columns = { live_column, after_column };
	      coefficients = { 1, -1 };
	      for (unsigned def_slot = 0; def_slot <= slot; ++def_slot)
		{
		  columns.push_back (x (value.def, def_slot));
		  coefficients.push_back (-1);
		}
	      if (!add_row (columns, coefficients, GE, -1))
		return false;
	    }
	}
    }

  if (live_in > m_problem.register_capacity)
    return false;

  for (unsigned slot = 0; slot != m_count; ++slot)
    {
      columns.clear ();
      coefficients.clear ();
      for (unsigned value = 0; value != m_value_count; ++value)
	{
	  columns.push_back (live (value, slot));
	  coefficients.push_back (1);
	}
      if (!add_row (columns, coefficients, LE,
		    m_problem.register_capacity))
	return false;
    }

  set_add_rowmode (m_lp, FALSE);

  /* lp_solve 5.5's column presolver does not reliably reconstruct variables
     fixed by our preferred-schedule certificate, and its dense elimination
     dominated compile time on a 17-op region.  Keep the explicit bounded
     model: fixed/list-feasible cases then solve directly, while hard cases
     remain protected by the branch-and-bound node cap.  */
  set_presolve (m_lp, PRESOLVE_NONE, 0);
  return true;
}

rvtt_solver_solution
lpsolve_model::solve ()
{
  rvtt_solver_solution solution;
  if (!valid_problem ())
    {
      solution.status = (m_count > 24 || m_value_count > 32
			 ? rvtt_solver_status::capped
			 : rvtt_solver_status::invalid_model);
      solution.diagnostic = "problem";
      return solution;
    }
  if (!build ())
    {
      solution.status = rvtt_solver_status::internal_error;
      solution.diagnostic = "build";
      return solution;
    }

  /* Prefer the deterministic list schedule with a numerically exact 0/1
     objective.  When that schedule already satisfies capacity it is the
     unique zero-cost optimum and gives lp_solve a trivial proof.  If it does
     not, the MILP is free to deviate and finds the fewest changed issue slots.
     This avoids the pathological sequence of lexicographic solves while
     retaining a real schedule-feasibility model.  */
  std::vector<int> objective_columns;
  std::vector<REAL> objective_coefficients;
  objective_columns.reserve (m_count * (m_count - 1));
  objective_coefficients.reserve (m_count * (m_count - 1));
  for (unsigned op = 0; op != m_count; ++op)
    for (unsigned slot = 0; slot != m_count; ++slot)
      if (slot != m_problem.preferred_slot[op])
	{
	  objective_columns.push_back (x (op, slot));
	  objective_coefficients.push_back (1);
	}
  if (!set_obj_fnex (m_lp, objective_columns.size (),
		     objective_coefficients.data (), objective_columns.data ()))
    {
      solution.status = rvtt_solver_status::internal_error;
      solution.diagnostic = "objective";
      return solution;
    }

  const int result = ::solve (m_lp);
  solution.solver_nodes = static_cast<unsigned> (get_total_nodes (m_lp));
  if (result == INFEASIBLE)
    {
      solution.status = rvtt_solver_status::infeasible;
      solution.diagnostic = "solve";
      return solution;
    }
  if (result != OPTIMAL)
    {
      solution.status = rvtt_solver_status::nonoptimal;
      solution.diagnostic = "solve";
      return solution;
    }

  std::vector<REAL> variables (m_columns);
  if (!get_variables (m_lp, variables.data ()))
    {
      solution.status = rvtt_solver_status::internal_error;
      solution.diagnostic = "variables";
      return solution;
    }

  solution.order.assign (m_count, std::numeric_limits<unsigned>::max ());
  for (unsigned op = 0; op != m_count; ++op)
    for (unsigned slot = 0; slot != m_count; ++slot)
      if (variables[x (op, slot) - 1] > 0.5)
	{
	  if (solution.order[slot] != std::numeric_limits<unsigned>::max ())
	    {
	      solution.status = rvtt_solver_status::internal_error;
	      solution.diagnostic = "duplicate-slot";
	      solution.order.clear ();
	      return solution;
	    }
	  solution.order[slot] = op;
	}

  if (std::find (solution.order.begin (), solution.order.end (),
		 std::numeric_limits<unsigned>::max ())
      != solution.order.end ())
    {
      solution.status = rvtt_solver_status::internal_error;
      solution.diagnostic = "incomplete-order";
      solution.order.clear ();
      return solution;
    }

  solution.status = rvtt_solver_status::optimal;
  solution.diagnostic = "ok";
  return solution;
}

} // anonymous namespace

rvtt_solver_solution
rvtt_lpsolve_schedule (const rvtt_sched_problem &problem)
{
  lpsolve_model model (problem);
  return model.solve ();
}

#endif /* HAVE_LPSOLVE */
