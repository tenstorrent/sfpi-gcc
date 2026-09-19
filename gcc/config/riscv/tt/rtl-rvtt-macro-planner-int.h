/* Internal interface between the two halves of the Tensix SFPLOADMACRO
   planner.

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

#ifndef GCC_RTL_RVTT_MACRO_PLANNER_INT_H
#define GCC_RTL_RVTT_MACRO_PLANNER_INT_H

/* For macro_region, macro_schedule, macro_descriptor,
   macro_residency_state and rvtt_macro::caps, which the declarations
   below are expressed in.  */
#include "rvtt-macro-desc.h"

/* rtl-rvtt-macro-planner.cc grew past 3400 lines.  The leaf layer --
   the configuration-ownership proofs, the delivery cost model and IMS
   profitability arbitration, the CC all-lanes ambient analysis and the
   RTL rewrite and emission primitives -- is in
   rtl-rvtt-macro-planner-cost.cc.  The formation driver, the upward-IMS
   carrier former and the pass glue stay in the original file.

   The dependency is one-directional: the cost half calls nothing in the
   driver half, so these are exports only.  No type crosses.  */

extern bool
planner_config_ownership_ok (function *fn, const rvtt_macro::caps *c);

extern bool
planner_config_window_ok (const macro_region &region);

extern bool
planner_region_config_ownership_ok (const macro_region &region,
				    basic_block config_preheader,
				    rtx_insn *scope_begin,
				    const rvtt_macro::caps *c);

extern bool
planned_value_dead_after_p (rtx value, rtx_insn *start);

extern unsigned
config_word_loadi_issues (uint32_t w);

extern bool
run_profitable_p (const macro_region &region, const macro_schedule &schedule,
		  const macro_descriptor &desc, unsigned run_rows,
		  int ih_stage, int64_t ih_entry, int64_t ih_body,
		  FILE *dump);

extern bool
ims_arbitrate_run (const macro_region &region, const macro_schedule &schedule,
		   const macro_descriptor &desc, unsigned run_rows,
		   int ih_stage, int64_t ih_entry, int64_t ih_body,
		   FILE *dump);

extern bool
ims_arbitrate_loop (const macro_region &region,
		    const macro_schedule &schedule,
		    const macro_descriptor &desc, gcov_type body_count,
		    gcov_type preheader_count, unsigned n_runs, FILE *dump);

extern bool
loop_trip_weight (basic_block body, basic_block preheader,
		  gcov_type *body_count, gcov_type *preheader_count);

extern bool
loop_profitable_p (const macro_region &region, const macro_schedule &schedule,
		   const macro_descriptor &desc, gcov_type body_count,
		   gcov_type preheader_count, unsigned n_runs);

extern bool
cc_enable_all_lanes_proved_p (rtx_insn *insn);

extern rtx_insn *
preheader_trailing_enable (basic_block preheader);

extern bool
entry_ambient_all_lanes_p (basic_block point_bb, rtx_insn *before,
			   FILE *dump);

extern basic_block
loop_region_preheader (function *fn, const macro_region &region, FILE *dump);

extern bool
planner_rewrite_load_addr_mode (rtx_insn *orig, rtx pat, unsigned addr_mode);

extern bool
planner_rewrite_dst_address (rtx_insn *orig, rtx pat, HOST_WIDE_INT new_addr);

extern void
emit_planner_run (macro_region &region, const macro_schedule &schedule,
		  const macro_descriptor &desc,
		  const rvtt_macro::caps *c,
		  unsigned begin, unsigned end, bool emit_config,
		  basic_block config_preheader, rtx_insn *enable_src,
		  basic_block hoist_preheader, rtx_insn *hoist_enable_src,
		  bool emit_drain,
		  /* Lane EV (P0 wrong-code fix, 2026-08-21): place this
		     many drain NOPs between consecutive rows of this
		     run -- the FULL derived drain when a fixed-VD VALUE
		     carrier's hosted events pend past the next row's
		     launch (see form_region for the derivation and
		     provenance), or the smaller residual
		     window-pairing tuning proved
		     (rvtt_macro_interrow_drain_tuned, under
		     -mtt-tensix-optimize-window-pairing).  */
		  int interrow_drain_slots,
		  /* Descriptor residency (rvtt-macro-desc.cc): elide the
		     descriptor words when a bit-identical dominating
		     resident program exists; collect the programming
		     insns (benign for later residency walks); report
		     where the words were programmed.  */
		  bool resident_elide, macro_residency_state *resid,
		  /* Lane CA cross-call init hoist: 0 = none, 1 = the
		     descriptor words live in the caller's preheader
		     (retain enable + owned SETC16 per call), 2 = the
		     full prefix lives there (emit nothing).  */
		  int init_hoist_stage,
		  basic_block *config_placement);

extern const char *
init_hoist_callee_scan (function *fn, const macro_region &region,
			rtx_insn **why_insn);

#endif /* GCC_RTL_RVTT_MACRO_PLANNER_INT_H */
