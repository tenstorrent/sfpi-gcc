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

/* Backend face of the IR-free arithmetic core
   (rvtt-delivery-cost-core.h).  rvtt-delivery-cost.cc is the ONLY
   translation unit that turns the rvtt-cost.md `define_constants'
   into the core's cost_table; every pricing consumer goes through
   these entry points (or carries the table in a solver problem, as
   rvtt-bnb.cc does).  */

#ifndef GCC_RVTT_DELIVERY_COST_H
#define GCC_RVTT_DELIVERY_COST_H

#include "rvtt-delivery-cost-core.h"

/* The audited cost table, built once from XTT_REPLAY_COST_*.  */
extern const rvtt_delivery_cost::cost_table &rvtt_dcost_table (void);

/* Centislot price of WORDS issue words on plane P.  */
extern int64_t rvtt_dcost_words_to_centislots (int64_t words,
					       rvtt_delivery_cost::plane p);

/* SFPLOADI issue-word count of materializing the 32-bit value W (the
   one spelling; see the core).  */
extern unsigned rvtt_dcost_loadi_issue_words (uint32_t w);

/* The replay-hoist minimum modeled benefit: the
   -mtt-tensix-replay-hoist-min-benefit= override when given, else
   XTT_REPLAY_HOIST_MIN_BENEFIT.  */
extern int64_t rvtt_dcost_replay_hoist_min_benefit (void);

/* Replay window pricing against the audited table (core
   replay_pricing over rvtt_dcost_table).  */
extern rvtt_delivery_cost::replay_price
rvtt_dcost_replay_pricing (rvtt_delivery_cost::replay_shape shape,
			   int64_t trips, int64_t words, int64_t exec_slots,
			   int64_t launch_run, bool drain_contract,
			   int64_t min_benefit);

/* Current-model Dst-auto-increment setup charge of a replay-delivered
   shape, centislots once per formed group (the delivery-shape solver's
   former W_drain MODEL SEAM, now this module's named quantity:
   rvtt-cost.md XTT_AUTOINCR_SETUP_COST_X100, current-model value 0 --
   the measured lane-EE table absorbs the SETC16 program in the
   once-per-group record delivery; a future hardware-measured value lands
   HERE and every consumer moves together).  */
extern int64_t rvtt_dcost_autoincr_setup_cost_x100 (void);

#ifdef RTX_CODE
/* The ONE word-exact replay comparator: PATTERN equality of two
   delivered Tensix words under the scratch-operand tolerance (compiler
   GPR scratch and synthesized-word MEMs do not reach the delivered
   word).  Replaces the three per-site spellings in
   rtl-rvtt-replay.cc.  */
extern bool rvtt_dcost_replay_word_equal_p (rtx_insn *a, rtx_insn *b);
#endif

#endif /* GCC_RVTT_DELIVERY_COST_H */
