/* Cross-loop hoist of loop-invariant Tensix SFPU materializations.
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

/* Cross-loop hoisting of constant SFPU materializations.

   The early invariant pass (gimple-rvtt-invariant.cc) hoists constant
   SFPLOADI materializations out of the loop that consumes them into that
   loop's preheader.  When the consuming loop is nested (a row loop inside
   a tile loop inside coarser loops), the preheader copy still runs once
   per iteration of each enclosing loop, because the enclosing loop bodies
   issue raw architectural words (MOP launches, RWC programming,
   synchronization, instruction-FIFO pushes) that the generic opaque-region
   and sfpu-barrier checks reject.

   This pass lifts those preheader-placed invariants across enclosing
   loops when rvtt_crossloop_region_scan (gimple-rvtt-crosscall-scan.cc)
   shows that no statement of the crossed loop body can write an
   allocatable LREG, change the SFPU CC/lane state, or issue an
   unrecognized or replay word; MOP words are checked against the TU-wide
   template scan.  Anything not recognized blocks the hoist.

   Only non-innermost loops are processed: what leaves the innermost loop
   is decided by the invariant pass's pressure-based selection.

   The motion must not be speculative: an LREG write may not execute on a
   path where it did not before.  Each candidate block must execute on the
   first iteration of the crossed loop (see
   rvtt_crossloop_block_executes_on_entry_p).  The eight-entry LREG file
   bounds how many values may stay live across the crossed loop.

   Refusal names reported in dumps (the region scan adds
   crossloop-{word,replay,config-word,cc,stmt,mop-slot}-unproven and
   crossloop-lreg-clobber):
     crossloop-preheader-unproven   no unique entry edge or blocked
				    insertion point
     crossloop-speculation-unproven candidate block not shown to
				    execute on first loop entry
     crossloop-pressure		    LREG file capacity exceeded  */

#define INCLUDE_VECTOR
#define INCLUDE_ALGORITHM
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
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
#include "tree-ssa-operands.h"
#include "tree-ssanames.h"
#include "cfghooks.h"
#include "cfgloop.h"
#include "cfganal.h"
#include "tree-cfg.h"
#include "dominance.h"
#include "rvtt-protos.h"
#include "rvtt-refuse.h"
#include "rvtt.h"
#include "rvtt-pressure.h"
#include "rvtt-macro-ownership.h"

namespace {

/* Allocatable hard-LREG mask: the crossed region must not write any
   register the allocator could assign to a hoisted value.  */
constexpr unsigned CROSSLOOP_ALLOCATABLE_MASK = 0x00ff;

/* Substitute the value X carries on the first header evaluation of
   LOOP entered through ENTRY (header-phi arguments from the entry
   edge), as rvtt_loop_first_iteration_executes_p does.  */

static tree
entry_value (class loop *loop, edge entry, tree x)
{
  if (TREE_CODE (x) != SSA_NAME)
    return x;
  gphi *phi = dyn_cast <gphi *> (SSA_NAME_DEF_STMT (x));
  if (!phi || gimple_bb (phi) != loop->header)
    return x;
  return PHI_ARG_DEF_FROM_EDGE (phi, entry);
}

/* Return true if guard edge GE controls BB: BB is dominated by GE's
   destination, and every other predecessor edge of that destination
   comes from a block the destination itself dominates (loop backedges
   and internal re-entries).  Then the first arrival at GE->dest on any
   path to BB came through GE, so GE's predicate held on every such path;
   a predicate over SSA operands, once true, stays true.  */

static bool
guard_edge_controls_p (edge ge, basic_block bb)
{
  if (!dominated_by_p (CDI_DOMINATORS, bb, ge->dest))
    return false;
  edge pe;
  edge_iterator ei;
  FOR_EACH_EDGE (pe, ei, ge->dest->preds)
    if (pe != ge && !dominated_by_p (CDI_DOMINATORS, pe->src, ge->dest))
      return false;
  return true;
}

/* Return the edge the first header test of LOOP takes when entered
   through ENTRY, or null if unknown.  Either the substituted test folds
   to a constant, or a dominating guard tests the same SSA operands with
   the same comparison and the guard edge that controls the loop entry
   fixes the outcome.  */

static edge
first_header_test_taken_edge (class loop *loop, edge entry)
{
  gimple_stmt_iterator last = gsi_last_bb (loop->header);
  gcond *cond = gsi_end_p (last)
    ? nullptr : dyn_cast <gcond *> (gsi_stmt (last));
  if (!cond)
    return nullptr;

  edge true_edge, false_edge;
  extract_true_false_edges_from_block (loop->header, &true_edge, &false_edge);
  if (!true_edge || !false_edge)
    return nullptr;

  tree lhs = entry_value (loop, entry, gimple_cond_lhs (cond));
  tree rhs = entry_value (loop, entry, gimple_cond_rhs (cond));
  tree_code code = gimple_cond_code (cond);

  tree folded = fold_binary (code, boolean_type_node, lhs, rhs);
  if (folded && TREE_CODE (folded) == INTEGER_CST)
    return integer_zerop (folded) ? false_edge : true_edge;

  /* Dominating-guard implication.  */
  for (basic_block dom = get_immediate_dominator (CDI_DOMINATORS, entry->src);
       dom; dom = get_immediate_dominator (CDI_DOMINATORS, dom))
    {
      gimple_stmt_iterator gl = gsi_last_bb (dom);
      gcond *guard = gsi_end_p (gl)
	? nullptr : dyn_cast <gcond *> (gsi_stmt (gl));
      if (!guard)
	continue;
      bool same = gimple_cond_code (guard) == code
	&& operand_equal_p (gimple_cond_lhs (guard), lhs, 0)
	&& operand_equal_p (gimple_cond_rhs (guard), rhs, 0);
      if (!same)
	continue;
      edge gtrue, gfalse;
      extract_true_false_edges_from_block (dom, &gtrue, &gfalse);
      if (gtrue && guard_edge_controls_p (gtrue, entry->src))
	return true_edge;
      if (gfalse && guard_edge_controls_p (gfalse, entry->src))
	return false_edge;
      /* An operand-identical guard whose edges do not pin the entry:
	 keep searching outer dominators.  */
    }
  return nullptr;
}

/* Return true if LOOP's header has an edge leaving the loop.  */

static bool
loop_header_has_exit_p (class loop *loop)
{
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, loop->header->succs)
    if (!flow_bb_inside_loop_p (loop, e->dest))
      return true;
  return false;
}

} /* anonymous namespace */

/* Return true if BB is known to execute on the first iteration of LOOP
   entered through ENTRY.  */

bool
rvtt_crossloop_block_executes_on_entry_p (class loop *loop, edge entry,
					  basic_block bb)
{
  /* BB executes on every entered iteration, every exit is from a block
     BB dominates, and the header itself has no exit (or the first
     iteration is known to execute): any terminating execution that
     enters the loop reaches an exit block, and BB dominates it.  */
  if (rvtt_stmt_executes_every_entered_iteration_p (loop, bb)
      && (!loop_header_has_exit_p (loop)
	  || rvtt_loop_first_iteration_executes_p (loop, entry)))
    return true;

  /* Otherwise, the first header test is known to branch into the body,
     and a single-successor chain from the taken edge reaches BB before
     any branching (hence before any exit) on that first iteration.  */
  edge taken = first_header_test_taken_edge (loop, entry);
  if (!taken || !flow_bb_inside_loop_p (loop, taken->dest))
    return false;
  basic_block cur = taken->dest;
  for (unsigned steps = 0; steps < 32; ++steps)
    {
      if (cur == bb)
	return true;
      if (!single_succ_p (cur))
	return false;
      cur = single_succ (cur);
      if (!flow_bb_inside_loop_p (loop, cur))
	return false;
    }
  return false;
}

/* Return the outermost enclosing entry edge to which a placement on
   LOOP's entry edge ENTRY may be lifted, given that the placed code
   writes the LREGs in LREG_MASK; CC_IMMATERIAL is passed to the region
   scan.  Callers use this only under riscv_tt_opt_crossloop_hoist.  */

edge
rvtt_crossloop_outermost_entry (class loop *loop, edge entry,
				unsigned lreg_mask, bool cc_immaterial)
{
  edge best = entry;
  class loop *l = loop;
  for (class loop *outer = loop_outer (loop); outer && outer->num;
       l = outer, outer = loop_outer (outer))
    {
      edge oentry = rvtt_loop_entry_edge (outer);
      const char *why = nullptr;
      gimple *why_stmt = nullptr;
      const char *refusal
	= !oentry ? "crossloop-preheader-unproven"
	: rvtt_preheader_insertion_blocked_p (oentry)
	  ? "crossloop-preheader-unproven"
	: !rvtt_crossloop_block_executes_on_entry_p (outer, oentry, l->header)
	  ? "crossloop-speculation-unproven"
	: !rvtt_crossloop_region_scan (outer, oentry, lreg_mask, &why,
				       &why_stmt, cc_immaterial)
	  ? why
	: nullptr;
      if (refusal)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "crossloop-hoist: placement walk stops at loop bb %d "
		     "(%s)\n", outer->header->index, refusal);
	  break;
	}
      best = oentry;
    }
  if (best != entry && dump_file)
    fprintf (dump_file,
	     "crossloop-hoist: placement lifted from entry bb %d to "
	     "entry bb %d\n", entry->dest->index, best->dest->index);
  return best;
}

namespace {

/* Select from LOADS the materializations that fit the LREG pressure of
   LOOP, greedily taking the most expensive first (by
   rvtt_sfpxloadi_materialization_cost, as the invariant pass does).  */

static auto_vec<gcall *>
select_pressure_legal (class loop *loop, auto_vec<gcall *> &loads)
{
  std::stable_sort (loads.begin (), loads.end (),
		    [] (gcall *a, gcall *b)
		    {
		      return rvtt_sfpxloadi_materialization_cost (a)
			> rvtt_sfpxloadi_materialization_cost (b);
		    });

  /* Compute the pressure profile once and query it incrementally per
     candidate.  */
  rvtt_loop_pressure profile (loop, /*cc_transients=*/false);
  auto_vec<gcall *> selected;
  for (gcall *call : loads)
    {
      selected.safe_push (call);
      if (!profile.legal_with (selected))
	{
	  selected.pop ();
	  if (dump_file)
	    {
	      fprintf (dump_file,
		       "crossloop-hoist: left in loop (crossloop-pressure): ");
	      print_gimple_stmt (dump_file, call, 0);
	    }
	}
    }
  return selected;
}

/* Run the cross-loop hoist over FN, innermost loops first.  For each
   loop that encloses another, collect the constant materializations in
   its direct body blocks that execute on first entry, check the crossed
   region and the preheader insertion point, filter by LREG pressure, and
   move the selected loads to the preheader.  Return true if the IL
   changed.  */

static bool
transform (function *fn)
{
  bool changed = false;
  if (!dom_info_available_p (CDI_DOMINATORS))
    calculate_dominance_info (CDI_DOMINATORS);

  /* Innermost first: a load lifted out of loop L lands in L's
     preheader, a direct body block of the enclosing loop, where it is
     considered again for that loop.  */
  for (class loop *loop : loops_list (fn, LI_FROM_INNERMOST))
    {
      /* Innermost loops are left to the invariant pass.  */
      if (!loop->num || !loop->inner)
	continue;

      edge entry = rvtt_loop_entry_edge (loop);
      if (!entry || rvtt_preheader_insertion_blocked_p (entry))
	{
	  rvtt_refuse (RVTT_REF_CROSSLOOP_PREHEADER_UNPROVEN, dump_file,
		       "crossloop-hoist: loop bb %d refused "
		       "(crossloop-preheader-unproven)\n", loop->header->index);
	  continue;
	}

      /* Candidates: constant materializations in the loop's direct body
	 (a load still inside a subloop was left there on purpose), each
	 in a block that executes on first loop entry.  */
      auto_vec<gcall *> loads;
      bool speculation_seen = false;
      basic_block *body = get_loop_body_in_dom_order (loop);
      for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
	{
	  basic_block bb = body[ix];
	  if (bb->loop_father != loop)
	    continue;
	  bool bb_checked = false, bb_ok = false;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	       !gsi_end_p (gsi); gsi_next (&gsi))
	    {
	      gcall *call = dyn_cast <gcall *> (gsi_stmt (gsi));
	      if (!call
		  || !rvtt_invariant_constant_load_p (call, loop,
						      /*allow_shortened=*/
						      true))
		continue;
	      if (!bb_checked)
		{
		  bb_checked = true;
		  bb_ok = rvtt_crossloop_block_executes_on_entry_p (loop,
								    entry,
								    bb);
		  if (!bb_ok)
		    speculation_seen = true;
		}
	      if (bb_ok)
		loads.safe_push (call);
	    }
	}
      free (body);

      if (loads.is_empty ())
	{
	  if (speculation_seen && dump_file)
	    rvtt_refuse (RVTT_REF_CROSSLOOP_SPECULATION_UNPROVEN, dump_file,
			 "crossloop-hoist: loop bb %d refused "
			 "(crossloop-speculation-unproven)\n",
			 loop->header->index);
	  continue;
	}

      /* No statement of the crossed loop (or the preheader tail) may
	 affect the allocatable LREGs, the CC/lane state, or the replay
	 buffer.  */
      const char *why = nullptr;
      gimple *why_stmt = nullptr;
      if (!rvtt_crossloop_region_scan (loop, entry,
				       CROSSLOOP_ALLOCATABLE_MASK,
				       &why, &why_stmt))
	{
	  rvtt_refuse_by_name (why ? why : "?", dump_file,
			       "crossloop-hoist: loop bb %d refused (%s)",
			       loop->header->index, why ? why : "?");
	  if (dump_file)
	    {
	      if (why_stmt)
		{
		  fprintf (dump_file, ": ");
		  print_gimple_stmt (dump_file, why_stmt, 0);
		}
	      else
		fprintf (dump_file, "\n");
	    }
	  continue;
	}

      auto_vec<gcall *> selected = select_pressure_legal (loop, loads);
      if (selected.is_empty ())
	continue;

      /* At least one load moves.  Split a shared entry edge only now,
	 so that refusals leave the CFG unchanged.  */
      basic_block preheader = rvtt_commit_hoist_preheader (entry);

      for (gcall *call : selected)
	{
	  gcall *root = rvtt_chained_loadi_root (call);
	  if (tree vdef = gimple_vdef (call))
	    {
	      if (TREE_CODE (vdef) == SSA_NAME)
		{
		  unlink_stmt_vdef (call);
		  release_ssa_name (vdef);
		}
	      gimple_set_vdef (call, NULL_TREE);
	    }
	  if (gimple_vuse (call))
	    {
	      gimple_set_vuse (call, NULL_TREE);
	      update_stmt (call);
	    }
	  if (root)
	    {
	      if (tree vdef = gimple_vdef (root))
		{
		  if (TREE_CODE (vdef) == SSA_NAME)
		    {
		      unlink_stmt_vdef (root);
		      release_ssa_name (vdef);
		    }
		  gimple_set_vdef (root, NULL_TREE);
		}
	      if (gimple_vuse (root))
		{
		  gimple_set_vuse (root, NULL_TREE);
		  update_stmt (root);
		}
	      gimple_stmt_iterator root_from = gsi_for_stmt (root);
	      gsi_move_to_bb_end (&root_from, preheader);
	    }

	  gimple_stmt_iterator from = gsi_for_stmt (call);
	  gsi_move_to_bb_end (&from, preheader);
	  changed = true;
	  if (dump_file)
	    {
	      fprintf (dump_file,
		       "crossloop-hoist: hoisted across loop bb %d to "
		       "preheader bb %d: ",
		       loop->header->index, preheader->index);
	      print_gimple_stmt (dump_file, call, 0);
	    }
	}
    }
  return changed;
}

const pass_data pass_data_rvtt_crossloop =
{
  GIMPLE_PASS,
  "rvtt_crossloop",
  OPTGROUP_OTHER,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_rvtt_crossloop : public gimple_opt_pass
{
public:
  pass_rvtt_crossloop (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_crossloop, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_crossloop_hoist > 0;
  }

  unsigned execute (function *fn) final override
  {
    if (TARGET_XTT_TENSIX_QSR)
      {
	rvtt_refuse (RVTT_REF_QSR_UNPROVEN, dump_file,
		     "crossloop-hoist: refused (qsr-unproven)\n");
	return 0;
      }
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    if (!dom_info_available_p (CDI_DOMINATORS))
      calculate_dominance_info (CDI_DOMINATORS);
    bool changed = transform (fn);
    loop_optimizer_finalize ();
    return changed ? TODO_update_ssa_only_virtuals : 0;
  }
};

} /* anonymous namespace */

/* Create the cross-loop hoist pass.  It runs after the cross-call hoist
   and before pass_rvtt_vif.  */

gimple_opt_pass *
make_pass_rvtt_crossloop (gcc::context *ctxt)
{
  return new pass_rvtt_crossloop (ctxt);
}
