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
