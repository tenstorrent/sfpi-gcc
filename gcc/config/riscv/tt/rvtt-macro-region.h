/* Macro-planner region discovery over typed effect sets (Layer 2).
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

#ifndef GCC_RVTT_MACRO_REGION_H
#define GCC_RVTT_MACRO_REGION_H

#include "rvtt-effects.h"

/* Regions are discovered from dataflow and typed effect sets only --
   never from opcode sequences, operation names, or raw encodings.  A row
   is a dataflow-closed slice from its Dst loads to its Dst store; rows
   group into runs when pairwise isomorphic to the first row under a
   value map with a uniform typed Dst stride; runs separated only by
   pure-RWC counter effects share one region.  v1 restricts regions to a
   single basic block; a self-looping block is marked as a loop body
   structurally (loop metadata is not available this late in the RTL
   pipeline, matching the discipline of the existing late passes).  */

struct macro_row
{
  /* Dataflow-closed row members, program order.  */
  vec<rtx_insn *> insns;
  /* Typed pure-RWC insn terminating the row, or NULL.  */
  rtx_insn *separator;
  /* Typed Dst stride established by the separator (0 when absent).  */
  int dst_delta;
  /* Value map proving isomorphism to rows[0]: (rows[0] reg, this reg)
     pairs, one per distinct register operand.  */
  vec<std::pair<rtx, rtx>> vmap;
};

struct macro_region
{
  basic_block bb;		/* v1: single BB.			*/
  bool loop_body;		/* structural self-loop marker		*/
  vec<macro_row> rows;
  vec<rtx_insn *> run_separators; /* pure-RWC insns between runs	*/
  unsigned runs;
  xtt_effect_set net;		/* union of row effects			*/
  uint32_t internal_lregs;	/* LREGs defined & consumed inside	*/
  rtx_insn *first, *last;
};

/* Stable refusal vocabulary (append-only; names are dump API).  */
enum class macro_region_refusal
{
  row_opaque_effect,
  row_not_closed,
  row_cc_write,
  row_config_write,
  row_not_isomorphic,
  row_stride_mismatch,
  row_live_through,
};

extern const char *macro_region_refusal_name (macro_region_refusal);

/* Analyze FN and report discovered regions and named refusals to DUMP.
   Analysis only: never mutates the function.  */
extern void rvtt_macro_region_analyze (function *fn, FILE *dump);

/* Same discovery, additionally collecting the clean regions into OUT
   (caller releases each with rvtt_macro_region_release).  */
extern void rvtt_macro_regions_discover (function *fn, FILE *dump,
					 vec<macro_region> *out);
extern void rvtt_macro_region_release (macro_region *region);

#endif /* GCC_RVTT_MACRO_REGION_H */
