/* Tensix counted-loop trip-count facade (dual-oracle, stage A).
   Copyright (C) 2022-2026 Tenstorrent Inc.

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

#ifndef GCC_RVTT_TRIPS_H
#define GCC_RVTT_TRIPS_H

/* The single entry point for proving the constant trip count (number
   of body executions) of a counted single-block Tensix loop, shared by
   the RTL replay formation machinery and the GIMPLE unroll-request /
   delivery-shape / round-interleave passes.

   Stage A contract (CLASS-I): every query runs BOTH the legacy bounded
   forward simulation (the deciding oracle -- its verdict and outputs
   are what the caller receives, byte-identically to the pre-facade
   passes) and the classical analysis (RTL loop-iv /
   get_simple_loop_desc, GIMPLE SCEV / number_of_latch_executions) as a
   cross-check.  Where both prove, the verdicts must be equal; a
   disagreement is dumped under the diagnostic name
   `trip-oracle-divergence' (a P1 -- the corpus census of that name
   must be EMPTY before stage B may flip the deciding oracle).
   One-sided proofs are dumped as `trip-oracle-legacy-only' /
   `trip-oracle-classical-only' census facts; agreement is dumped as
   `trip-oracle-agree'.  All facts go to the running pass's dump file
   only; generated code never depends on the classical oracle in
   stage A.  */

/* RTL face: prove the constant trip count of single-block LOOP whose
   dedicated preheader is PREHEADER.  Return true and set *TRIPS
   (number of times the loop body executes) on success.  On success the
   optional outputs receive the loop's single counter-step insn
   (*STEP_OUT) and the counter's proven value at loop exit (*FINAL_OUT,
   reduced to the counter mode's precision).  */
extern bool rvtt_loop_trips (class loop *loop, basic_block preheader,
			     uint64_t *trips, rtx_insn **step_out = nullptr,
			     uint64_t *final_out = nullptr);

/* GIMPLE face: prove the constant trip count (body executions) of
   LOOP.  Return true and set *TRIPS on success.  */
extern bool rvtt_loop_trips_gimple (class loop *loop,
				    unsigned HOST_WIDE_INT *trips);

/* Walk backwards from the end of PREHEADER through the
   unique-predecessor chain looking for the last definition of REG.
   Return true and set *VALUE if that definition is a simple constant
   load; refuse on any other definition or on a call.  (The legacy
   reaching-definition proof; also used directly by the peel
   admission's counter re-init check.)  */
extern bool rvtt_constant_reaching_value (basic_block preheader, rtx reg,
					  uint64_t *value);

#endif /* GCC_RVTT_TRIPS_H */
