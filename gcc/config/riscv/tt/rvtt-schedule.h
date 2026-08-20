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

#include <utility>
#include <vector>

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

#endif /* GCC_RVTT_SCHEDULE_H */
