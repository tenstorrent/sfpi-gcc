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

/* This translation unit is the ONLY consumer that turns the
   rvtt-cost.md `define_constants' delivery economics into arithmetic
   (via the IR-free core, rvtt-delivery-cost-core.h).  New pricing
   arithmetic belongs here, never in a pass.  */

#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tm_p.h"
#include "rvtt-delivery-cost.h"

using namespace rvtt_delivery_cost;

/* The audited cost table, built once from the rvtt-cost.md
   define_constants (insn-constants.h).  */

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

/* Centislot price of delivering WORDS issue words on plane P (RISC
   push vs replay slot), at the audited per-plane rate.  */

int64_t
rvtt_dcost_words_to_centislots (int64_t words, plane p)
{
  return words_to_centislots (rvtt_dcost_table (), words, p);
}

/* SFPLOADI issue-word count (1 or 2) of materializing the 32-bit
   value W through an LReg -- 1 when W fits one of the single-issue
   immediate encodings, 2 otherwise (see loadi_issue_words in
   rvtt-delivery-cost-core.h for the encoding forms).  */

unsigned
rvtt_dcost_loadi_issue_words (uint32_t w)
{
  return loadi_issue_words (w);
}

/* Minimum modeled centislot benefit a replay hoist must clear: the
   -mtt-tensix-replay-hoist-min-benefit= override when non-negative,
   else the audited rvtt-cost.md default.  */

int64_t
rvtt_dcost_replay_hoist_min_benefit (void)
{
  return riscv_tt_replay_hoist_min_benefit >= 0
    ? (int64_t) riscv_tt_replay_hoist_min_benefit
    : (int64_t) XTT_REPLAY_HOIST_MIN_BENEFIT;
}

/* Price one replay window against the audited cost table: SHAPE's
   before/after delivery arithmetic over TRIPS trips of WORDS payload
   words executing in EXEC_SLOTS slots, with LAUNCH_RUN feeding the
   delivery-bound saturation term and DRAIN_CONTRACT selecting the
   completion guard's full-record charge.  Returns every priced term
   plus the profitability verdict against MIN_BENEFIT (core
   replay_pricing bound to rvtt_dcost_table).  */

replay_price
rvtt_dcost_replay_pricing (replay_shape shape, int64_t trips, int64_t words,
			   int64_t exec_slots, int64_t launch_run,
			   bool drain_contract, int64_t min_benefit)
{
  return replay_pricing (rvtt_dcost_table (), shape, trips, words,
			 exec_slots, launch_run, drain_contract,
			 min_benefit);
}

/* Once-per-formed-group Dst-auto-increment setup charge, in
   centislots x100.  A named constant rather than arithmetic: the
   current rvtt-cost.md model prices it at 0 (the measured record
   delivery absorbs the SETC16 program); every consumer reads it from
   here so a future non-zero silicon-priced value moves them
   together.  */

int64_t
rvtt_dcost_autoincr_setup_cost_x100 (void)
{
  return XTT_AUTOINCR_SETUP_COST_X100;
}

/* The one word-exact replay comparator (previously spelled three times
   in rtl-rvtt-replay.cc: the discovery bucket confirm, the reform-mode
   carried-payload audit, and the window-sizing re-verification).  The
   scratch-operand tolerance admits exactly the operand classes that do
   not reach the delivered Tensix word: compiler GPR scratch
   (CLOBBER/SCRATCH) and synthesized-word SImode MEMs (a non-SImode MEM
   is (probably) broken code attempting to spill/fill an LReg and never
   compares equal).  */

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
