/* Delivery-cost interface for the Tensix backend.
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

/* Backend wrapper around the IR-free arithmetic core
   (rvtt-delivery-cost-core.h).  rvtt-delivery-cost.cc is the only
   translation unit that turns the rvtt-cost.md `define_constants'
   into the core's cost_table; pricing consumers go through these
   entry points (or carry the table in a solver problem, as
   rvtt-bnb.cc does).  */

#ifndef GCC_RVTT_DELIVERY_COST_H
#define GCC_RVTT_DELIVERY_COST_H

#include "rvtt-delivery-cost-core.h"

/* The cost table, built once from XTT_REPLAY_COST_*.  */
extern const rvtt_delivery_cost::cost_table &rvtt_dcost_table (void);

/* Centislot price of WORDS issue words on plane P.  */
extern int64_t rvtt_dcost_words_to_centislots (int64_t words,
					       rvtt_delivery_cost::plane p);

/* SFPLOADI issue-word count of materializing the 32-bit value W.  */
extern unsigned rvtt_dcost_loadi_issue_words (uint32_t w);

#ifdef RTX_CODE
#endif

#endif /* GCC_RVTT_DELIVERY_COST_H */
