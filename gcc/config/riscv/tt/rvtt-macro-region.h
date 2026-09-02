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
  /* Ambient pure-CC-write lane enable preceding the row, or NULL.  */
  rtx_insn *enable;
  /* First row of a new run within the region.  */
  bool starts_run;
  /* Typed Dst stride established by the separator (0 when absent).  */
  int dst_delta;
  /* Immediate Dst-address delta of this row's typed Dst accesses
     relative to rows[0]: the loop-fusion
     passes carry part of the per-row Dst advance in the address
     immediates instead of a separator.  Zero for the classic
     separator-carried shape.  Admitted only under the region-level
     uniform absolute-progression proof (finalize_region), and only
     ever emitted through the absorbed-stride calendar (formation
     refuses otherwise).  */
  int imm_delta;
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
  /* Uniform per-row absolute Dst advance for regions whose rows carry
     immediate address deltas (macro_row::imm_delta); 0 for the classic
     separator-carried shape.  Proven by finalize_region's absolute-
     progression check: row k's accumulated separator advance plus its
     immediate delta equals k * imm_stride for every row, and the
     region's total separator advance equals rows * imm_stride (the
     downstream counter state the absorbed-stride emission
     reproduces).  */
  int imm_stride;
};

/* Stable refusal vocabulary (append-only; names are dump API).  */
enum class macro_region_refusal
{
  row_opaque_effect,
  row_not_closed,
  /* A CC-writing value event inside the row slice would need a
     CC-manipulating instruction template; no such template program is
     proven, so the row refuses by that missing capability.  (Emitted
     name supersedes the earlier "row-cc-write".)  */
  row_cc_template_unsupported,
  row_config_write,
  row_not_isomorphic,
  row_stride_mismatch,
  row_live_through,
  /* A pure CC write whose written lane state is not provably the
     all-lanes enable (rvtt_insn_effects's cc_write_all_lanes, derived
     word-exact from the capability table's architectural SFPENCC
     encoding).  Such a write can never serve as an ambient enable and
     invalidates any earlier one, so it is a hard region boundary.  */
  row_cc_enable_unproved,
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
