/* Tensix counted-loop trip-count analysis.
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

/* The constant trip count (number of body executions) of a counted
   single-block Tensix loop, proved by a bounded forward simulation of its
   counter.  Shared by the RTL replay formation machinery and the GIMPLE
   Tensix loop passes.  */

/* Prove the constant trip count of single-block LOOP whose
   dedicated preheader is PREHEADER.  Return true and set *TRIPS
   (number of times the loop body executes) on success.  On success the
   optional outputs receive the loop's single counter-step insn
   (*STEP_OUT) and the counter's proven value at loop exit (*FINAL_OUT,
   reduced to the counter mode's precision).  */
extern bool rvtt_loop_trips (class loop *loop, basic_block preheader,
			     uint64_t *trips, rtx_insn **step_out = nullptr,
			     uint64_t *final_out = nullptr);

/* Prove the constant trip count (body executions) of GIMPLE loop
   LOOP.  Return true and set *TRIPS on success.  */
extern bool rvtt_loop_trips_gimple (class loop *loop,
				    unsigned HOST_WIDE_INT *trips);

/* Walk backwards from the end of PREHEADER through the unique-predecessor
   chain looking for the last definition of REG.  Return true and set *VALUE if
   that definition is a simple constant load; refuse on any other definition or
   on a call.  Exported for callers that need to check a counter's
   re-initialization.  */
extern bool rvtt_constant_reaching_value (basic_block preheader, rtx reg,
					  uint64_t *value);

#endif /* GCC_RVTT_TRIPS_H */
