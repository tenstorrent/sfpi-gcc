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

/* The shared constant-trip-count proof for counted single-block Tensix
   loops, used by the RTL replay formation passes and the GIMPLE Tensix loop
   passes.  The proof is a bounded forward simulation of the loop counter.
   RTL loop-iv cannot be used here: the replay passes run after reload, and
   loop-iv refuses hard-register counters.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "tree.h"
#include "gimple.h"
#include "tree-pass.h"
#include "ssa.h"
#include "cfghooks.h"
#include "cfgloop.h"
#include "cfgrtl.h"
#include "dominance.h"
#include "df.h"
#include "rvtt-protos.h"
#include "rvtt-trips.h"

/* A trip count is provable when:

   - the loop is a single basic block ending in a two-way conditional jump;
   - exactly one comparison operand is a counter register with exactly one
     in-loop modification, a reg = reg + const step;
   - the other comparison operand is a constant, either immediate or a
     register with no in-loop modification whose last definition on the
     unique preheader path is a constant load;
   - the counter's own last definition on that path is a constant load.

   The iteration is then evaluated directly, wrapping at the register mode's
   precision, until the continue condition first fails.  Anything else,
   including a merely estimated profile count, is treated as unknown.  */

/* Walk backwards from the end of PREHEADER through the unique-predecessor
   chain looking for the last definition of REG.  Return true and set *VALUE
   if that definition is a simple constant load; refuse on any other
   definition or on a call (potential clobber).  Only a unique predecessor is
   followed, so the result is a reaching definition rather than a guess; the
   visited set stops on cyclic predecessor chains.  */
bool
rvtt_constant_reaching_value (basic_block preheader, rtx reg, uint64_t *value)
{
  basic_block bb = preheader;
  auto_bitmap visited;
  while (bitmap_set_bit (visited, bb->index))
    {
      rtx_insn *insn;
      FOR_BB_INSNS_REVERSE (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  if (CALL_P (insn))
	    return false;
	  if (!reg_set_p (reg, insn))
	    continue;
	  rtx set = single_set (insn);
	  if (!set || !REG_P (SET_DEST (set))
	      || REGNO (SET_DEST (set)) != REGNO (reg)
	      || !CONST_INT_P (SET_SRC (set)))
	    return false;
	  *value = UINTVAL (SET_SRC (set));
	  return true;
	}
      if (!single_pred_p (bb))
	return false;
      bb = single_pred (bb);
    }
  return false;
}

/* Evaluate an integer condition CODE on VAL0, VAL1, both already reduced to
   PREC-bit values.  Signed comparisons sign-extend from PREC.  */
static bool
eval_int_condition (rtx_code code, uint64_t val0, uint64_t val1,
		    unsigned prec)
{
  int64_t s0 = val0, s1 = val1;
  if (prec < 64)
    {
      uint64_t sign = uint64_t (1) << (prec - 1);
      s0 = int64_t ((val0 ^ sign) - sign);
      s1 = int64_t ((val1 ^ sign) - sign);
    }
  switch (code)
    {
    case EQ: return val0 == val1;
    case NE: return val0 != val1;
    case LT: return s0 < s1;
    case LE: return s0 <= s1;
    case GT: return s0 > s1;
    case GE: return s0 >= s1;
    case LTU: return val0 < val1;
    case LEU: return val0 <= val1;
    case GTU: return val0 > val1;
    case GEU: return val0 >= val1;
    default: return false;
    }
}

/* Prove the constant trip count of single-block LOOP whose dedicated
   preheader is PREHEADER.  Return true and set *TRIPS (number of times the
   loop body executes) on success; any structural mismatch refuses.  On
   success the optional outputs receive the loop's single counter-step insn
   (*STEP_OUT) and the counter's proven value at loop exit (*FINAL_OUT,
   reduced to the counter mode's precision); the launch-loop unroll uses
   them to replace the removed per-trip updates.  */
static bool
simulate_constant_trips (class loop *loop, basic_block preheader,
		       uint64_t *trips, rtx_insn **step_out,
		       uint64_t *final_out)
{
  basic_block header = loop->header;
  rtx_insn *jump = BB_END (header);
  if (!JUMP_P (jump) || !any_condjump_p (jump) || !onlyjump_p (jump)
      || EDGE_COUNT (header->succs) != 2)
    return false;

  edge e_branch = BRANCH_EDGE (header);
  edge e_fall = FALLTHRU_EDGE (header);
  bool taken_continues;
  if (e_branch->dest == header && e_fall->dest != header)
    taken_continues = true;
  else if (e_fall->dest == header && e_branch->dest != header)
    taken_continues = false;
  else
    return false;

  rtx set = pc_set (jump);
  if (!set)
    return false;
  rtx src = SET_SRC (set);
  if (GET_CODE (src) != IF_THEN_ELSE)
    return false;
  rtx cond = XEXP (src, 0);
  if (!COMPARISON_P (cond))
    return false;
  /* Branch taken when the condition holds, unless the label is in the
     else arm.  */
  bool taken_when_true = GET_CODE (XEXP (src, 1)) != PC;

  rtx op0 = XEXP (cond, 0);
  rtx op1 = XEXP (cond, 1);

  /* Identify the counter operand: a hard register with exactly one in-loop
     modification of the form reg = reg + const.  */
  rtx counter = nullptr, bound = nullptr;
  rtx_insn *step_insn = nullptr;
  for (int side = 0; side != 2; ++side)
    {
      rtx cand = side ? op1 : op0;
      if (!REG_P (cand))
	continue;
      rtx_insn *insn;
      rtx_insn *found = nullptr;
      bool bad = false;
      FOR_BB_INSNS (header, insn)
	if (NONDEBUG_INSN_P (insn) && insn != jump
	    && reg_set_p (cand, insn))
	  {
	    if (found)
	      bad = true;
	    found = insn;
	  }
      if (bad)
	return false;
      if (found)
	{
	  if (counter)
	    /* Both operands are modified in the loop.  */
	    return false;
	  counter = cand;
	  bound = side ? op0 : op1;
	  step_insn = found;
	}
    }
  if (!counter)
    return false;

  rtx step_set = single_set (step_insn);
  if (!step_set || !REG_P (SET_DEST (step_set))
      || REGNO (SET_DEST (step_set)) != REGNO (counter)
      || GET_CODE (SET_SRC (step_set)) != PLUS
      || !REG_P (XEXP (SET_SRC (step_set), 0))
      || REGNO (XEXP (SET_SRC (step_set), 0)) != REGNO (counter)
      || !CONST_INT_P (XEXP (SET_SRC (step_set), 1)))
    return false;
  uint64_t step = UINTVAL (XEXP (SET_SRC (step_set), 1));

  scalar_int_mode mode;
  if (!is_a<scalar_int_mode> (GET_MODE (counter), &mode)
      || GET_MODE_PRECISION (mode) > 64)
    return false;
  unsigned prec = GET_MODE_PRECISION (mode);
  uint64_t mask = prec == 64 ? ~uint64_t (0)
    : (uint64_t (1) << prec) - 1;

  uint64_t init;
  if (!rvtt_constant_reaching_value (preheader, counter, &init))
    return false;

  uint64_t bound_val;
  if (CONST_INT_P (bound))
    bound_val = UINTVAL (bound);
  else if (REG_P (bound))
    {
      /* The bound must be loop-invariant with a provable constant value.  */
      rtx_insn *insn;
      FOR_BB_INSNS (header, insn)
	if (NONDEBUG_INSN_P (insn) && insn != jump
	    && reg_set_p (bound, insn))
	  return false;
      if (!rvtt_constant_reaching_value (preheader, bound, &bound_val))
	return false;
    }
  else
    return false;
  bound_val &= mask;

  /* Directly evaluate the counter chain, wrapping at the mode precision,
     until the continue condition first fails.  */
  bool counter_is_op0 = rtx_equal_p (counter, op0);
  uint64_t c = init & mask;
  constexpr uint64_t TRIP_BOUND = uint64_t (1) << 16;
  for (uint64_t t = 1; t <= TRIP_BOUND; ++t)
    {
      c = (c + step) & mask;
      uint64_t v0 = counter_is_op0 ? c : bound_val;
      uint64_t v1 = counter_is_op0 ? bound_val : c;
      bool cond_holds = eval_int_condition (GET_CODE (cond), v0, v1, prec);
      bool taken = cond_holds == taken_when_true;
      bool continues = taken == taken_continues;
      if (!continues)
	{
	  *trips = t;
	  if (step_out)
	    *step_out = step_insn;
	  if (final_out)
	    *final_out = c;
	  return true;
	}
    }
  return false;
}

/* Prove the constant trip count of single-block LOOP whose dedicated
   preheader is PREHEADER.  On success return true, set *TRIPS, and fill the
   optional *STEP_OUT (the loop's single counter-step insn) and *FINAL_OUT
   (the counter's proven value at loop exit).  */

bool
rvtt_loop_trips (class loop *loop, basic_block preheader, uint64_t *trips,
		 rtx_insn **step_out, uint64_t *final_out)
{
  uint64_t n = 0;
  rtx_insn *step = nullptr;
  uint64_t final_value = 0;
  if (!simulate_constant_trips (loop, preheader, &n, &step, &final_value))
    return false;
  *trips = n;
  if (step_out)
    *step_out = step;
  if (final_out)
    *final_out = final_value;
  return true;
}

/* Prove the constant trip count (body executions) of LOOP at GIMPLE,
   returning true and setting *TRIPS on success.  */

bool
rvtt_loop_trips_gimple (class loop *loop, unsigned HOST_WIDE_INT *trips)
{
  return rvtt_replay_unroll_counted_trips (loop, trips);
}
