/* The one delivery-cost API of the Tensix backend.
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

/* Wrappers that bind the IR-free delivery-cost arithmetic in
   rvtt-delivery-cost-core.h to the cost constants defined in rvtt-cost.md.
   Passes price instruction delivery through these functions rather than
   doing their own arithmetic on the constants.  */

#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tm_p.h"
#include "rvtt-delivery-cost.h"

using namespace rvtt_delivery_cost;

/* Return the cost table, built once from the rvtt-cost.md
   define_constants.  */

const cost_table &
rvtt_dcost_table (void)
{
  static const cost_table t
    = { XTT_REPLAY_COST_RISC_PUSH_X100,
	XTT_REPLAY_COST_REPLAY_SLOT_X100,
	XTT_REPLAY_COST_TURNAROUND_X100,
	XTT_REPLAY_COST_RECORD_OVERHEAD_X100 };
  return t;
}

/* Return the cost in centislots of delivering WORDS issue words on
   plane P (RISC push or replay slot).  */

int64_t
rvtt_dcost_words_to_centislots (int64_t words, plane p)
{
  return words_to_centislots (rvtt_dcost_table (), words, p);
}

/* Return the number of SFPLOADI issue words (1 or 2) needed to load the
   32-bit value W into an LReg.  */

unsigned
rvtt_dcost_loadi_issue_words (uint32_t w)
{
  return loadi_issue_words (w);
}

/* Return the minimum benefit in centislots a replay hoist must reach:
   -mtt-tensix-replay-hoist-min-benefit= when non-negative, else the
   rvtt-cost.md default.  */

int64_t
rvtt_dcost_replay_hoist_min_benefit (void)
{
  return riscv_tt_replay_hoist_min_benefit >= 0
    ? (int64_t) riscv_tt_replay_hoist_min_benefit
    : (int64_t) XTT_REPLAY_HOIST_MIN_BENEFIT;
}

/* Price one replay window of shape SHAPE: TRIPS trips of WORDS payload
   words executing in EXEC_SLOTS slots.  LAUNCH_RUN feeds the
   delivery-bound saturation term and DRAIN_CONTRACT selects the
   completion guard's full-record charge.  Return every priced term and
   whether the window clears MIN_BENEFIT.  */

replay_price
rvtt_dcost_replay_pricing (replay_shape shape, int64_t trips, int64_t words,
			   int64_t exec_slots, int64_t launch_run,
			   bool drain_contract, int64_t min_benefit)
{
  return replay_pricing (rvtt_dcost_table (), shape, trips, words,
			 exec_slots, launch_run, drain_contract,
			 min_benefit);
}

/* Return true if insns A and B deliver the same Tensix instruction word.
   Operands that do not reach the delivered word are ignored: GPR scratch
   (CLOBBER/SCRATCH) and the SImode MEMs of synthesized words.  A non-SImode
   MEM is most likely an attempt to spill or fill an LReg and never
   compares equal.  */

bool
rvtt_dcost_replay_word_equal_p (rtx_insn *a, rtx_insn *b)
{
  auto ignore = [] (const_rtx *x, const_rtx *y, rtx *nx, rtx *ny)
    {
      if (GET_CODE (*x) != GET_CODE (*y))
	return false;
      if (GET_CODE (*x) == MEM)
	{
	  if (GET_MODE (*x) != SImode)
	    return false;
	}
      else if (GET_CODE (*x) != CLOBBER && GET_CODE (*x) != SCRATCH)
	return false;
      gcc_checking_assert (GET_MODE (*x) == GET_MODE (*y));
      *nx = *ny = nullptr;
      return true;
    };
  return rtx_equal_p (PATTERN (a), PATTERN (b), ignore);
}
