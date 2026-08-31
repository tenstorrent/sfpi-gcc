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

/* The early invariant pass (gimple-rvtt-invariant.cc) hoists constant
   SFPU materializations out of the loop that consumes them -- into
   that loop's preheader.  When the consuming loop is nested (an LLK
   row loop inside a tile loop inside coarser delivery loops), the
   preheader placement still re-executes the materializations once per
   ENCLOSING-loop iteration, because the enclosing loop bodies deliver
   raw architectural words (MOP launches, RWC programming,
   synchronization, instruction-FIFO pushes) that the shared
   opaque-region and sfpu-barrier proofs refuse wholesale.

   This pass lifts those already-preheader-placed invariants across
   enclosing loops under an AUDITED region discipline
   (rvtt_crossloop_region_scan, gimple-rvtt-crosscall.cc): every
   statement of the crossed loop body must be proven unable to write an
   allocatable LREG, unable to change the SFPU CC/lane state, and
   unable to deliver an unaudited or replay word; a MOP word defers to
   the TU-wide template census.  Refusing default for every class not
   on record; a refusal never edits anything.

   Scope discipline: only non-innermost loops are processed.
   Materialization out of the innermost (defining) loop is the early
   invariant pass's decision -- its pressure pricing chose what stays
   in the row body, and re-litigating that here would disturb
   downstream consumers (e.g. the programmable-constant fusion) that
   depend on the row-level placement.

   No-speculation obligation: an architectural LREG write must never
   execute on a path where the original would not have.  Each candidate
   block must provably execute on the first iteration of the crossed
   loop entered through its unique entry edge
   (rvtt_crossloop_block_executes_on_entry_p): either the
   executes-every-entered-iteration dominance proof combined with a
   loop shape whose header has no exit (rotated do-while -- entering
   the loop reaches an exit only through blocks the candidate
   dominates), or the first header test folds -- or is implied by a
   dominating guard over the same SSA operands -- toward the body and a
   single-successor chain from the taken edge reaches the candidate
   block.

   Refusal taxonomy (dump-stable names; the region-scan names
   crossloop-{word,replay,config-word,cc,stmt,mop-slot}-unproven and
   crossloop-lreg-clobber come from the shared scan):
     crossloop-preheader-unproven   no unique entry edge or blocked
				    insertion point
     crossloop-speculation-unproven candidate block not proven to
				    execute on first loop entry
     crossloop-pressure		    eight-LREG file exceeded (per-load,
				    greedy)
   QSR refuses by pass gate (no validated capability).  */

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
#include "rvtt-macro-ownership.h"

namespace {

/* Allocatable hard-LREG mask: the audited region must be unable to
   write any register the allocator could assign to a hoisted value.  */
constexpr unsigned CROSSLOOP_ALLOCATABLE_MASK = 0x00ff;

/* Substitute the value X carries on the first header evaluation of
   LOOP entered through ENTRY (header-phi arguments from the entry
   edge), mirroring rvtt_loop_first_iteration_executes_p.  */

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

/* The guard edge GE controls BB: BB is dominated by GE's destination,
   and every other predecessor edge of that destination comes from a
   block the destination itself dominates (loop backedges and internal
   re-entries).  Then the FIRST arrival at GE->dest on any path to BB
   came through GE, so GE's predicate held on every such path -- and a
   predicate over SSA operands, once true, stays true (SSA values are
   immutable).  */

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

/* The edge the first header test of LOOP takes when entered through
   ENTRY, or null when unprovable.  Two proofs, both refusing default:
   the substituted test folds to a constant; or a dominating guard
   tests the SAME SSA operands with the same comparison, and the guard
   edge that controls the loop entry pins the outcome (SSA names are
   immutable, so operand identity is value identity).  */

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

/* Whether LOOP's header has an edge leaving the loop.  */

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

} // anonymous namespace

/* BB provably executes on the first iteration of LOOP entered through
   ENTRY (rvtt-macro-ownership.h).  */

bool
rvtt_crossloop_block_executes_on_entry_p (class loop *loop, edge entry,
					  basic_block bb)
{
  /* Arm 1: BB executes on every entered iteration, and every exit is
     from a block BB dominates (the proof's own discipline, header
     exempt) while the header itself has no exit: any terminating
     execution that enters the loop reaches an exit block, and
     dominance of that exit block by BB means it passed BB.  */
  if (rvtt_stmt_executes_every_entered_iteration_p (loop, bb)
      && (!loop_header_has_exit_p (loop)
	  || rvtt_loop_first_iteration_executes_p (loop, entry)))
    return true;

  /* Arm 2: the first header test provably branches toward the body,
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

/* The outermost enclosing entry edge a loop-entry placement of LOOP
   may be lifted to (rvtt-macro-ownership.h).  Consumers (the
   programmable-constant passes) call this only under
   riscv_tt_opt_crossloop_hoist.  */

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

/* Greedy pressure-legal selection, most expensive materializations
   first (the invariant pass's policy, its cost model shared through
   rvtt_sfpxloadi_materialization_cost).  */

static auto_vec<gcall *>
select_pressure_legal (class loop *loop, auto_vec<gcall *> &loads)
{
  std::stable_sort (loads.begin (), loads.end (),
		    [] (gcall *a, gcall *b)
		    {
		      return rvtt_sfpxloadi_materialization_cost (a)
			> rvtt_sfpxloadi_materialization_cost (b);
		    });

  auto_vec<gcall *> selected;
  for (gcall *call : loads)
    {
      selected.safe_push (call);
      if (!rvtt_loop_lreg_pressure_legal_p (loop, selected, false))
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

static bool
transform (function *fn)
{
  bool changed = false;
  if (!dom_info_available_p (CDI_DOMINATORS))
    calculate_dominance_info (CDI_DOMINATORS);

  /* Innermost first: a load lifted out of loop L lands in L's
     preheader, a direct body block of the enclosing loop, where the
     enclosing loop's own proofs decide whether it moves again.  Every
     proof re-runs on the CFG as already transformed.  */
  for (class loop *loop : loops_list (fn, LI_FROM_INNERMOST))
    {
      /* Enclosing-loop lift only: materialization out of the innermost
	 (defining) loop is the early invariant pass's priced decision
	 (file header).  */
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

      /* Candidates: qualifying constant materializations in the
	 loop's DIRECT body (a load still inside a subloop was placed
	 there deliberately), each in a block proven to execute on
	 first loop entry.  */
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

      /* The audited region proof: every statement of the crossed loop
	 (and the preheader tail) proven inert for the allocatable
	 LREG file, the CC/lane state, and the replay buffer.  */
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

      /* Commit: all proofs hold and at least one load moves.  A shared
	 entry edge is split only now, keeping every refusal above
	 byte-identical to the flag-off compilation.  */
      basic_block preheader = rvtt_commit_hoist_preheader (entry);

      for (gcall *call : selected)
	{
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
    return changed ? TODO_update_ssa_only_virtuals | TODO_verify_all : 0;
  }
};

} // anonymous namespace

gimple_opt_pass *
make_pass_rvtt_crossloop (gcc::context *ctxt)
{
  return new pass_rvtt_crossloop (ctxt);
}
