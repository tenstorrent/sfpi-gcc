/* Cross-call constant delivery: the exported region scan
   Copyright (C) 2022-2026 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).
   Rewritten Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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

/* rvtt_crossloop_region_scan, the audited hoist-region scan the
   cross-loop consumers call (gimple-rvtt-crossloop.cc and the
   invariant pass's region discipline): one walk of a loop body
   under the crosscall scanner's admission vocabulary, refusing by
   name.  Split from gimple-rvtt-crosscall.cc; the algorithm essay
   lives there.  */

#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "rtl.h"
#include "tree.h"
#include "fold-const.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "ssa-iterators.h"
#include "tree-into-ssa.h"
#include "tree-ssanames.h"
#include "tree-eh.h"
#include "gimplify.h"
#include "cfghooks.h"
#include "cfgloop.h"
#include "cfganal.h"
#include "tree-cfg.h"
#include "tree-dfa.h"
#include "dominance.h"
#include "cgraph.h"
#include "stringpool.h"
#include "attribs.h"
#include "insn-codes.h"
#include "insn-config.h"
#include "recog.h"
#include "rvtt-protos.h"
#include "rvtt.h"
#include "rvtt-pressure.h"
#include "rvtt-refuse.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-macro-ownership.h"
#include "rvtt-macro-tables.h"
#include "rvtt-mop-tables.h"
#include "rvtt-mop-derive.h"
#include "rvtt-ipa-summary.h"
#include "rvtt-cc-region.h"
#include "rvtt-raw-boundary.h"
#include "gimple-rvtt-crosscall-int.h"

/* Audited hoist-region scan for the cross-loop hoist consumers
   (rvtt-macro-ownership.h).  The region is {LOOP body} union
   {preheader tail at/after the ENTRY insertion point} -- the same
   region rvtt_loop_hoist_region_opaque_p covers -- walked under the
   region discipline of scan_stmt: vector dataflow is
   register-allocation visible and admitted; CC writes, replay words,
   delivered SFPCONFIG words, unaudited words/calls/asm, explicit
   hard-LREG writes into LREG_MASK, and side-effecting typed calls
   beyond the explicit Dst boundary set all refuse by name.  A MOP word
   defers to the TU template census (LREG face) against LREG_MASK.  */

bool
rvtt_crossloop_region_scan (class loop *loop, edge entry, unsigned lreg_mask,
			    const char **why, gimple **why_stmt,
			    bool cc_immaterial)
{
  compute_tu_facts ();

  /* The verdict below leans on the TU census (a MOP word defers to the
     template audit; the extern-fixed-surface axiom covers only rooted
     bodies, and the census SKIPS bodies outside the rooted closure
     entirely).  The function being edited must itself be a closure
     member -- an unrooted body (a naked-asm-entry TU, an unrooted
     census) was never audited, so nothing vouches for the region.
     Fail closed by name.  */
  cgraph_node *self = cfun ? cgraph_node::get (cfun->decl) : nullptr;
  if (tu_facts.census_unrooted || !self
      || !tu_facts.executable->contains (self))
    {
      if (dump_file)
	fprintf (dump_file,
		 "crossloop-hoist: editing function %s outside the rooted "
		 "census closure (crossloop-caller-unrooted)\n",
		 self ? self->dump_name () : "?");
      if (why)
	*why = "crossloop-caller-unrooted";
      if (why_stmt)
	*why_stmt = nullptr;
      return false;
    }

  scan_ctx ctx;
  ctx.contract_mask = lreg_mask;
  ctx.callee_decl = NULL_TREE;
  ctx.in_caller = false;
  ctx.region = true;
  ctx.cc_immaterial = cc_immaterial;

  /* The crossloop-cc-unproven widening: under
     -mtt-tensix-optimize-cc-region-general, a crossed loop whose CC
     activity the CC-region tree proves ambient-preserving-and-
     narrowing admits its typed structured-CC atoms -- the enable set
     at every in-loop point stays a subset of the lifted entry's
     ambient, which is exactly the containment fact the consumers'
     all-lanes hoisted writes need.  Computed once per scanned loop;
     fail-closed to the standing refusal (with its own name) when the
     tree cannot prove the loop.  */
  if (riscv_tt_opt_cc_region_general > 0 && !cc_immaterial)
    {
      rvtt_cc_region_tree ccr (cfun);
      /* Two tree-proven admissions, either sufficient:
	 - the lifted entry edge carries the ALL-LANES state (kill-
	   modeling backward proof): a placement there writes EVERY
	   lane, so any crossed CC activity leaves the consumers'
	   enable sets subsets of the placement's -- the containment
	   fact holds unconditionally and the typed-atom whitelist
	   below is the only remaining discipline;
	 - the crossed loop's CC activity is ambient-preserving-and-
	   narrowing (balanced structured frames; pre-canonicalization
	   pipeline positions).  */
      bool entry_all = ccr.edge_entry_all_lanes_p (entry);
      ctx.cc_ambient_ok = entry_all
	|| ccr.loop_cc_ambient_preserving_p (loop);
      if (dump_file && ctx.cc_ambient_ok)
	fprintf (dump_file,
		 entry_all
		 ? "crossloop-hoist: entry bb %d proven ALL-LANES "
		   "(cc-region-general): crossed CC atoms admitted\n"
		 : "crossloop-hoist: loop bb %d CC activity tree-proven "
		   "ambient-preserving (cc-region-general)\n",
		 entry_all ? entry->dest->index : loop->header->index);
    }

  bool ok = true;
  basic_block *body = get_loop_body (loop);
  for (unsigned ix = 0; ix != loop->num_nodes && ok; ++ix)
    for (gimple_stmt_iterator gsi = gsi_start_bb (body[ix]);
	 !gsi_end_p (gsi) && ok; gsi_next (&gsi))
      ok = scan_stmt (&ctx, gsi_stmt (gsi), /*in_caller=*/false);
  free (body);

  /* Preheader tail at/after the hoist insertion point: with
     end-of-block insertion only a block-terminating statement can
     execute after the hoisted statements.  */
  if (ok && single_succ_p (entry->src))
    {
      gimple_stmt_iterator last = gsi_last_nondebug_bb (entry->src);
      if (!gsi_end_p (last) && stmt_ends_bb_p (gsi_stmt (last)))
	ok = scan_stmt (&ctx, gsi_stmt (last), /*in_caller=*/false);
    }

  if (ok && ctx.saw_mop)
    {
      const char *mop_why = nullptr;
      if (!mop_contract_ok_p (lreg_mask, &mop_why))
	{
	  if (dump_file && mop_why)
	    fprintf (dump_file, "crossloop-hoist:   (%s)\n", mop_why);
	  ctx.why = "crossloop-mop-slot-unproven";
	  ctx.why_stmt = nullptr;
	  ok = false;
	}
    }

  if (!ok)
    {
      if (why)
	*why = ctx.why;
      if (why_stmt)
	*why_stmt = ctx.why_stmt;
    }
  return ok;
}
