/* PRGM constant programming: constant residency
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

/* Constant residency for the PRGM constant pass
   (-mtt-tensix-optimize-const-residency).  Loop-invariant and repeated
   constant materializations are replaced by reads of PRGM constant registers
   that are programmed once, outside the loop or at function entry.  When a
   loop body writes CC, the first iteration is peeled to obtain an all-lanes
   programming point.  The overall design of the pass is described in
   gimple-rvtt-prgm-const.cc.  */

#define INCLUDE_VECTOR
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
#include "tree-ssa-loop-niter.h"
#include "tree-scalar-evolution.h"
#include "cfganal.h"
#include "tree-cfg.h"
#include "dominance.h"
#include "cgraph.h"
#include "stringpool.h"
#include "attribs.h"
#include "rvtt-protos.h"
#include "rvtt-refuse.h"
#include "rvtt.h"
#include "rvtt-pressure.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-placement.h"
#include "rvtt-macro-ownership.h"
#include "rvtt-mop-tables.h"
#include "rvtt-mop-derive.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-ipa-summary.h"
#include "gimple-rvtt-prgm-int.h"

/* ------------------------------------------------------------------ */
/* Residency allocation: place proven-constant values in free PRGM registers.
   Class LOOP: an invariant constant materialization inside a loop is
   programmed once on the loop entry edge, saving its SFPLOADI words on every
   iteration (delivery model in rvtt-cost.md); only a loop proven to run a
   single trip is refused.  Class PRESSURE: under LREG over-pressure an
   out-of-loop constant is reprogrammed in place, since a constant register
   read occupies no allocatable LREG.  */

struct residency_candidate
{
  gcall *load;			/* the single-issue materialization */
  unsigned value;		/* full 32-bit lane image */
  class loop *loop;		/* LOOP class: the enclosing loop */
  edge entry;			/* LOOP class: its entry edge */
  unsigned uses;		/* non-debug uses (the ranking key) */
  bool peel = false;		/* LOOP class: CC-canonical body; the
				   programming point is created by
				   peeling the first iteration */
  bool cc_lifted = false;	/* LOOP class: programming placed at
				   ENTRY, an edge proven at discovery
				   (the enclosing-loop entry under
				   -mtt-tensix-optimize-crossloop-cc-peel,
				   or the loop's own entry); no peel */
  bool inplace = false;		/* MAD-PAIR and HOISTED-REUSE classes:
				   program at the hoisted
				   materialization's own position, as
				   for the PRESSURE class */
  unsigned group = 0;		/* MAD-PAIR class: the constants of one
				   mul+add pair are placed together or
				   not at all, since a half-claimed pair
				   still blocks the mad combine */
  bool hoisted_reuse = false;	/* HOISTED-REUSE class: a hoisted
				   invariant materialization moved into
				   a PRGM register to free its loop-wide
				   LREG */
  bool call_free_window = false; /* No foreign call or asm runs between
				   the programming point and the in-loop
				   readers.  Required to reclaim a
				   claimed but unread register for a
				   different value, so that no callee can
				   reprogram it in between; same-value
				   reuse does not need it.  */
  bool own_entry = false;	/* LOOP class: a CC-canonical body
				   programmed on its own all-lanes entry
				   edge instead of being peeled */
};

/* Fold the in-loop chain from a header PHI to OP: assignments with exactly one
   non-invariant operand.  Return the value of OP on the first iteration
   entered through ENTRY, or NULL_TREE.  Scalar evolution and
   loop_niter_by_eval cannot be used here because they require LOOPS_NORMAL
   preheaders, which this pass does not establish.  */

static tree
first_iteration_value (class loop *loop, edge entry, tree op)
{
  if (is_gimple_min_invariant (op))
    return op;
  /* Walk the definition chain back to a header PHI, then fold forward from the
     entry value.  The depth bound only limits the work; the counted-loop exit
     tests of interest are one or two statements deep.  */
  auto_vec<gimple *, 8> chain;
  tree cur = op;
  for (unsigned depth = 0; depth != 8; ++depth)
    {
      if (TREE_CODE (cur) != SSA_NAME)
	return NULL_TREE;
      gimple *def = SSA_NAME_DEF_STMT (cur);
      if (gphi *phi = dyn_cast <gphi *> (def))
	{
	  if (gimple_bb (phi) != loop->header)
	    return NULL_TREE;
	  tree value = PHI_ARG_DEF_FROM_EDGE (phi, entry);
	  if (!is_gimple_min_invariant (value))
	    return NULL_TREE;
	  /* Fold the chain forward, substituting VALUE for each
	     statement's single non-invariant operand.  */
	  for (int ix = chain.length () - 1; ix >= 0; --ix)
	    {
	      gassign *a = as_a <gassign *> (chain[ix]);
	      tree_code code = gimple_assign_rhs_code (a);
	      tree type = TREE_TYPE (gimple_assign_lhs (a));
	      tree op1 = gimple_assign_rhs1 (a);
	      tree op2 = gimple_num_ops (a) > 2
		? gimple_assign_rhs2 (a) : NULL_TREE;
	      if (!is_gimple_min_invariant (op1))
		op1 = value;
	      if (op2 && !is_gimple_min_invariant (op2))
		op2 = value;
	      switch (get_gimple_rhs_class (code))
		{
		case GIMPLE_SINGLE_RHS:
		  value = op1;
		  break;
		case GIMPLE_UNARY_RHS:
		  value = fold_unary (code, type, op1);
		  break;
		case GIMPLE_BINARY_RHS:
		  value = fold_binary (code, type, op1, op2);
		  break;
		default:
		  return NULL_TREE;
		}
	      if (!value || !is_gimple_min_invariant (value))
		return NULL_TREE;
	    }
	  return value;
	}
      gassign *assign = dyn_cast <gassign *> (def);
      if (!assign || !gimple_bb (assign)
	  || !flow_bb_inside_loop_p (loop, gimple_bb (assign)))
	return NULL_TREE;
      /* Exactly one non-invariant operand continues the chain.  */
      tree next = NULL_TREE;
      for (unsigned i = 1; i < gimple_num_ops (assign); ++i)
	{
	  tree o = gimple_op (assign, i);
	  if (!o || is_gimple_min_invariant (o))
	    continue;
	  if (next)
	    return NULL_TREE;
	  next = o;
	}
      if (!next)
	return NULL_TREE;
      chain.safe_push (assign);
      cur = next;
    }
  return NULL_TREE;
}

/* Trip-count classes for LOOP-class profitability.  The classification only
   prices: correctness does not depend on the trip count, because the
   programming point is on the entry edge of a rotated loop (reaching it means
   the header executes) and nothing in an admitted body clobbers the register.

   TRIPS_AT_LEAST_2: the exit test stays in the loop after the first iteration,
   so the programming pays for itself.
   TRIPS_PROVEN_SINGLE: the loop exits after one iteration and the programming
   cost is never recovered; the candidate is refused.  Such loops are normally
   fully unrolled earlier.
   TRIPS_UNKNOWN: the test does not fold (a runtime trip count); admitted, at
   worst one extra pushed word on a single-trip entry.  The peel class
   duplicates the body and so needs a proven trip count instead.  */

enum loop_trip_class
{
  TRIPS_AT_LEAST_2,
  TRIPS_PROVEN_SINGLE,
  TRIPS_UNKNOWN
};

/* Classify LOOP against the two-trip break-even (see the comment
   above the enum): fold its single exit test with every operand
   replaced by its first-iteration value as seen through ENTRY.  A
   loop with multiple exits, a non-GIMPLE_COND exit, or an unfoldable
   test answers TRIPS_UNKNOWN.  */

static loop_trip_class
classify_second_trip (class loop *loop, edge entry)
{
  auto_vec<edge> exits = get_loop_exit_edges (loop);
  if (exits.length () != 1)
    return TRIPS_UNKNOWN;
  edge exit = exits[0];
  gimple_stmt_iterator last = gsi_last_bb (exit->src);
  gcond *cond = gsi_end_p (last) ? nullptr
    : dyn_cast <gcond *> (gsi_stmt (last));
  if (!cond)
    return TRIPS_UNKNOWN;
  tree lhs = first_iteration_value (loop, entry, gimple_cond_lhs (cond));
  tree rhs = first_iteration_value (loop, entry, gimple_cond_rhs (cond));
  if (!lhs || !rhs)
    return TRIPS_UNKNOWN;
  tree test = fold_binary (gimple_cond_code (cond), boolean_type_node,
			   lhs, rhs);
  if (!test || TREE_CODE (test) != INTEGER_CST)
    return TRIPS_UNKNOWN;
  edge true_edge, false_edge;
  extract_true_false_edges_from_block (exit->src, &true_edge, &false_edge);
  edge taken = integer_zerop (test) ? false_edge : true_edge;
  if (!taken)
    return TRIPS_UNKNOWN;
  return taken == exit ? TRIPS_PROVEN_SINGLE : TRIPS_AT_LEAST_2;
}

/* Number of non-debug statements using the SSA name NAME.  Counts
   statements, not operands: a statement reading NAME twice counts
   once.  */

static unsigned
count_nondebug_uses (tree name)
{
  unsigned n = 0;
  imm_use_iterator iter;
  gimple *use;
  FOR_EACH_IMM_USE_STMT (use, iter, name)
    if (!is_gimple_debug (use))
      ++n;
  return n;
}

/* ------------------------------------------------------------------ */
/* CC-canonical loops: first-iteration peel.

   The LOOP class needs a loop and function free of CC writes, because the
   entry-edge programming executes under the loop-entry lane state and each
   replaced materialization must have executed under that same state.  Kernel
   row loops typically contain a lowered v_if region and rematerialize their
   constants every row.

   For a single-block body whose last CC writer is the all-lanes SFPENCC
   (rvtt_loop_cc_canonical_body), peeling the first iteration makes the
   transformation exact:

   - the peeled copy reproduces iteration one statement for statement, whatever
     the ambient CC state, and ends in the all-lanes state;
   - the SFPLOADI + SFPCONFIG programming follows the peeled copy and so
     executes with all lanes enabled, as SFPCONFIG requires (it copies L0
     lane-wise, so the staged value must be present in every lane);
   - every later iteration starts in the all-lanes state, so a candidate placed
     before the body's first CC writer wrote every lane, and the
     constant-register read yields the same value.

   Profitability follows the residency-peel model in rvtt-cost.md: the peeled
   body is delivered as pushed words and the programming costs one push per
   staged word and per SFPCONFIG, against the materialization words saved on
   every remaining iteration.  The required trip count is proven by bounded
   evaluation of the loop's scalar control.  */

/* Issue words of the materialization CALL after immediate shortening.  */

static unsigned
loadi_issue_words (gcall *call)
{
  return rvtt_sfpxloadi_materialization_cost (call);
}

/* Return true if LOOP's body provably executes at least NEED times, by bounded
   forward evaluation of the single-block body's scalar control from the values
   on ENTRY.  Statements and header PHIs that do not fold just become unknown;
   the proof fails when the exit test does not fold or the loop provably exits
   earlier.  */

static bool
loop_trips_at_least_p (class loop *loop, edge entry, unsigned need)
{
  if (need <= 1)
    return true;

  basic_block bb = loop->header;
  auto_vec<edge> exits = get_loop_exit_edges (loop);
  if (exits.length () != 1 || exits[0]->src != bb)
    return false;
  edge exit = exits[0];
  edge latch_e = loop_latch_edge (loop);
  gimple_stmt_iterator last = gsi_last_bb (bb);
  gcond *cond = gsi_end_p (last) ? nullptr
    : dyn_cast <gcond *> (gsi_stmt (last));
  if (!cond || !latch_e)
    return false;

  edge true_edge, false_edge;
  extract_true_false_edges_from_block (bb, &true_edge, &false_edge);
  if (!true_edge || !false_edge)
    return false;

  /* Current values of the header PHIs (and, within an iteration, of
     folded body definitions).  A PHI whose entry value does not fold
     (e.g. a loop-carried vector) simply stays unknown; the proof fails
     only when the exit test itself needs an unknown value.  */
  hash_map<tree, tree> vals;
  auto_vec<gphi *, 4> phis;
  for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
       gsi_next (&psi))
    {
      gphi *phi = psi.phi ();
      tree res = gimple_phi_result (phi);
      if (virtual_operand_p (res))
	continue;
      tree init = PHI_ARG_DEF_FROM_EDGE (phi, entry);
      if (!is_gimple_min_invariant (init))
	continue;
      phis.safe_push (phi);
      vals.put (res, init);
    }

  auto lookup = [&vals] (tree op) -> tree
    {
      if (!op || is_gimple_min_invariant (op))
	return op;
      if (TREE_CODE (op) != SSA_NAME)
	return NULL_TREE;
      tree *v = vals.get (op);
      return v ? *v : NULL_TREE;
    };

  bool proven = true;
  fold_defer_overflow_warnings ();
  for (unsigned trips = 1; trips < need; ++trips)
    {
      /* One pass over the body: fold what folds.  */
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gassign *a = dyn_cast <gassign *> (gsi_stmt (gsi));
	  if (!a)
	    continue;
	  tree lhs = gimple_assign_lhs (a);
	  if (!lhs || TREE_CODE (lhs) != SSA_NAME)
	    continue;
	  tree_code code = gimple_assign_rhs_code (a);
	  tree type = TREE_TYPE (lhs);
	  tree op1 = lookup (gimple_assign_rhs1 (a));
	  tree op2 = gimple_num_ops (a) > 2
	    ? lookup (gimple_assign_rhs2 (a)) : NULL_TREE;
	  tree value = NULL_TREE;
	  switch (get_gimple_rhs_class (code))
	    {
	    case GIMPLE_SINGLE_RHS:
	      value = op1;
	      break;
	    case GIMPLE_UNARY_RHS:
	      value = op1 ? fold_unary (code, type, op1) : NULL_TREE;
	      break;
	    case GIMPLE_BINARY_RHS:
	      value = op1 && op2
		? fold_binary (code, type, op1, op2) : NULL_TREE;
	      break;
	    default:
	      value = NULL_TREE;
	      break;
	    }
	  if (value && is_gimple_min_invariant (value))
	    vals.put (lhs, value);
	  else
	    vals.remove (lhs);
	}

      tree lhs = lookup (gimple_cond_lhs (cond));
      tree rhs = lookup (gimple_cond_rhs (cond));
      tree test = lhs && rhs
	? fold_binary (gimple_cond_code (cond), boolean_type_node, lhs, rhs)
	: NULL_TREE;
      if (!test || TREE_CODE (test) != INTEGER_CST)
	{
	  proven = false;
	  break;
	}
      edge taken = integer_zerop (test) ? false_edge : true_edge;
      if (taken == exit)
	{
	  proven = false;	/* provably exits before NEED trips */
	  break;
	}

      /* Advance the tracked header PHIs through the latch; one whose
	 next value does not fold merely stops being tracked.  */
      auto_vec<tree, 4> next;
      for (gphi *phi : phis)
	next.safe_push (lookup (PHI_ARG_DEF_FROM_EDGE (phi, latch_e)));
      /* Body definitions do not survive the backedge.  */
      vals.empty ();
      unsigned kept = 0;
      for (unsigned ix = 0; ix != phis.length (); ++ix)
	if (next[ix])
	  {
	    vals.put (gimple_phi_result (phis[ix]), next[ix]);
	    phis[kept++] = phis[ix];
	  }
      phis.truncate (kept);
    }
  fold_undefer_and_ignore_overflow_warnings ();
  return proven;
}

/* Duplicate LOOP's single-block body once onto ENTRY and return the new entry
   edge (peeled block -> header), where the caller places the programming.  The
   caller has proven the body CC-canonical and that the first iteration does
   not exit, so the copied exit test is dropped (its scalar chain is still
   copied for the header PHIs).  Header PHIs take their ENTRY arguments inside
   the copy and are re-seeded with the copy's latch values.  Virtual operands
   on the copies are cleared for the pass-level virtual SSA update.

   *OUT_COPY_BB receives the peeled block and NAMES maps each original SSA name
   (and header PHI result) to its copy; the pressure-park tier uses them to
   erase a duplicated materialization.  */

static edge
peel_first_iteration (class loop *loop, edge entry,
		      basic_block *out_copy_bb, hash_map<tree, tree> *names)
{
  basic_block bb = loop->header;
  edge latch_e = loop_latch_edge (loop);

  hash_map<tree, tree> &map = *names;
  for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
       gsi_next (&psi))
    {
      gphi *phi = psi.phi ();
      tree res = gimple_phi_result (phi);
      if (!virtual_operand_p (res))
	map.put (res, PHI_ARG_DEF_FROM_EDGE (phi, entry));
    }

  basic_block copy_bb = split_edge (entry);
  gimple_stmt_iterator at = gsi_start_bb (copy_bb);

  auto remap = [&map] (tree op) -> tree
    {
      if (op && TREE_CODE (op) == SSA_NAME)
	if (tree *found = map.get (op))
	  return *found;
      return op;
    };

  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
	  || gimple_code (stmt) == GIMPLE_COND)
	continue;

      gimple *cp = gimple_copy (stmt);

      /* Remap operands, then give each definition a fresh name.  */
      if (gcall *call = dyn_cast <gcall *> (cp))
	{
	  for (unsigned ix = 0; ix != gimple_call_num_args (call); ++ix)
	    gimple_call_set_arg (call, ix, remap (gimple_call_arg (call, ix)));
	}
      else if (gassign *a = dyn_cast <gassign *> (cp))
	{
	  for (unsigned ix = 1; ix != gimple_num_ops (a); ++ix)
	    gimple_set_op (a, ix, remap (gimple_op (a, ix)));
	}
      /* Audited raw Dst/RWC words carry one constant input: nothing to
	 remap and nothing defined.  */

      if (tree lhs = gimple_get_lhs (cp))
	{
	  gcc_assert (TREE_CODE (lhs) == SSA_NAME);
	  tree fresh = make_ssa_name (TREE_TYPE (lhs));
	  gimple_set_lhs (cp, fresh);
	  map.put (lhs, fresh);
	}

      if (gimple_vdef (cp))
	gimple_set_vdef (cp, NULL_TREE);
      if (gimple_vuse (cp))
	gimple_set_vuse (cp, NULL_TREE);

      if (gsi_end_p (at))
	{
	  gsi_insert_before (&at, cp, GSI_NEW_STMT);
	  at = gsi_for_stmt (cp);
	}
      else
	gsi_insert_after (&at, cp, GSI_NEW_STMT);
      update_stmt (cp);
    }

  if (out_copy_bb)
    *out_copy_bb = copy_bb;

  /* The loop's entry values are now the peeled iteration's latch
     values.  */
  edge new_entry = single_succ_edge (copy_bb);
  for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
       gsi_next (&psi))
    {
      gphi *phi = psi.phi ();
      if (virtual_operand_p (gimple_phi_result (phi)))
	continue;
      tree larg = PHI_ARG_DEF_FROM_EDGE (phi, latch_e);
      SET_USE (PHI_ARG_DEF_PTR_FROM_EDGE (phi, new_entry), remap (larg));
    }

  if (dump_file)
    fprintf (dump_file,
	     "const-residency: peeled first iteration of loop bb %d into "
	     "bb %d (CC-canonical body; programming point follows the "
	     "peeled all-lanes SFPENCC)\n",
	     bb->index, copy_bb->index);
  return new_entry;
}

/* -------------------------------------------------------------------- */
/* Pre-peel lane-state proof.

   The pressure-park tier can hoist a constant to the post-peel programming
   point, but the peeled copy keeps its own materialization, so the constant is
   materialized twice per loop entry.  Placing the definition at the head of
   the peel block instead lets the copy be erased, but only if all lanes are
   enabled there.

   That holds when no function-local CC-affecting statement reaches the point
   without passing an all-lanes SFPENCC, which overwrites the lane enables from
   its immediates.  cc_write_reaches_point_p cannot be used because it does not
   model that kill: a nested CC-canonical loop's own writers reach its
   preheader around the enclosing backedge.

   Calls are treated as CC-transparent, as in the plain LOOP class.  Raw asm is
   transparent because the transform only runs when the TU raw-boundary audit
   passed (facts.refused), and no audited raw word (NOP, sync, thread config,
   CLEARDVALID, SETRWC, LOADI, SFPCONFIG) writes lane state.  When the proof
   fails the post-peel placement is kept.  */

enum cc_block_class
{
  CC_BLOCK_TRANSPARENT,		/* no CC-affecting statement */
  CC_BLOCK_KILLS,		/* last CC event is the all-lanes SFPENCC */
  CC_BLOCK_DIRTY		/* a CC-affecting statement escapes */
};

/* Classify BB by its last CC-affecting statement: CC_BLOCK_KILLS when that is
   the all-lanes SFPENCC, CC_BLOCK_DIRTY when another CC writer (or a
   pushc/popc) follows the last kill, and CC_BLOCK_TRANSPARENT when the block
   has no typed CC event.  Calls and raw asm are transparent (see above).  */

static cc_block_class
classify_cc_block (basic_block bb)
{
  cc_block_class cls = CC_BLOCK_TRANSPARENT;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
	  || gimple_code (stmt) == GIMPLE_COND)
	continue;
      const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
      if (!insnd)
	continue;		/* calls and raw asm: see above */
      gcall *call = as_a <gcall *> (stmt);
      if (rvtt_all_lanes_encc_p (stmt))
	cls = CC_BLOCK_KILLS;
      else if (insnd->sets_cc (call)
	       || insnd->id == rvtt_insn_data::sfppushc
	       || insnd->id == rvtt_insn_data::sfppopc)
	cls = CC_BLOCK_DIRTY;
    }
  return cls;
}

/* Whether the lane-enable state on entry to POINT_BB is provably the
   architectural all-lanes state: every backwards CFG path from
   POINT_BB's entry hits the function entry (all-lanes ambient) or an
   all-lanes-SFPENCC-terminated block before any escaping CC-affecting
   statement.  */

static bool
prepeel_ambient_all_lanes_p (basic_block point_bb)
{
  hash_set<basic_block> visited;
  auto_vec<basic_block, 16> work;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, point_bb->preds)
    work.safe_push (e->src);
  while (!work.is_empty ())
    {
      basic_block b = work.pop ();
      if (b == ENTRY_BLOCK_PTR_FOR_FN (cfun))
	continue;
      if (visited.add (b))
	continue;
      switch (classify_cc_block (b))
	{
	case CC_BLOCK_DIRTY:
	  return false;
	case CC_BLOCK_KILLS:
	  continue;
	case CC_BLOCK_TRANSPARENT:
	  FOR_EACH_EDGE (e, ei, b->preds)
	    work.safe_push (e->src);
	  break;
	}
    }
  return true;
}

/* Whether the lane-enable state on exit from BB is provably all-lanes:
   BB itself ends on the all-lanes SFPENCC, or it has no CC event and
   its entry state is proven (prepeel_ambient_all_lanes_p).  */

static bool
block_exit_ambient_all_lanes_p (basic_block bb)
{
  switch (classify_cc_block (bb))
    {
    case CC_BLOCK_KILLS:
      return true;
    case CC_BLOCK_DIRTY:
      return false;
    case CC_BLOCK_TRANSPARENT:
      break;
    }
  return prepeel_ambient_all_lanes_p (bb);
}

/* True when USE is a SET_CC compare reading NAME as its VD.  It is
   lane-predicated like the VC form (SFPGT.md, SFPLE.md), but a VD of L12
   or above suppresses the compare, so such a constant can only stay
   resident in an LREG.  */

static bool
compare_vd_use_p (gimple *use, tree name)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (use);
  if (!insnd
      || (insnd->id != rvtt_insn_data::sfpgt
	  && insnd->id != rvtt_insn_data::sfple))
    return false;
  gcall *call = as_a <gcall *> (use);
  tree mod = gimple_call_arg (call, gimple_call_num_args (call) - 1);
  return TREE_CODE (mod) == INTEGER_CST && TREE_INT_CST_LOW (mod) == 1
	 && gimple_call_arg (call, 0) == name
	 && gimple_call_arg (call, 1) != name;
}

/* LOOP class candidate discovery: collect the loop-class,
   MAD-pair and hoisted-reuse candidates of FN's loops into LOOP_CANDS,
   MADPAIR_CANDS and HOISTREUSE_CANDS (MADPAIR_GROUP numbers the pair
   groups; TAKEN marks claimed statements).  */

static void
residency_collect_loop_class (function *fn,
			      auto_vec<residency_candidate> &loop_cands,
			      auto_vec<residency_candidate> &madpair_cands,
			      auto_vec<residency_candidate> &hoistreuse_cands,
			      unsigned &madpair_group,
			      hash_set<gimple *> &taken)
{
  for (class loop *loop : loops_list (fn, LI_FROM_INNERMOST))
    {
      if (!loop->num)
	continue;
      edge entry = rvtt_loop_entry_edge (loop);
      bool peel = false;
      gimple *cc_limit = nullptr;
      const char *why
	= !entry ? "no-single-entry"
	: rvtt_loop_hoist_region_opaque_p (loop, entry) ? "opaque-hoist-region"
	: rvtt_preheader_insertion_blocked_p (entry) ? "preheader-blocked"
	: nullptr;
      if (!why && rvtt_loop_has_sfpu_barrier_p (loop))
	{
	  /* A single-block body whose only barrier statements are CC writers
	     ending in the all-lanes SFPENCC can be peeled; candidates must
	     then precede the body's first CC writer.  */
	  rvtt_cc_canonical_body canon = rvtt_loop_cc_canonical_body (loop);
	  if (canon.proven)
	    {
	      peel = true;
	      cc_limit = canon.first_cc_writer;
	    }
	  else
	    {
	      if (dump_file)
		fprintf (dump_file,
			 "const-residency: loop bb %d cc-canonical proof "
			 "failed (%s)\n", loop->header->index, canon.why);
	      why = "sfpu-barrier";
	    }
	}
      if (why)
	{
	  rvtt_refuse_by_name (why, dump_file,
			       "const-residency: loop bb %d refused (%s)\n",
			       loop->header->index, why);
	  continue;
	}

      /* Profitability only: correctness does not depend on the trip count (see
	 classify_second_trip).  A loop proven to run once is refused; a
	 runtime trip count is admitted.  The peel class is priced separately
	 below.  */
      bool admits_runtime_trips = false;
      if (!peel)
	switch (classify_second_trip (loop, entry))
	  {
	  case TRIPS_AT_LEAST_2:
	    break;
	  case TRIPS_PROVEN_SINGLE:
	    rvtt_refuse (RVTT_REF_TRIP_COUNT_SINGLE_TRIP, dump_file,
			 "const-residency: loop bb %d refused "
			 "(trip-count-single-trip: the loop provably runs "
			 "one trip; the programming can never recover its "
			 "cost)\n", loop->header->index);
	    continue;
	  case TRIPS_UNKNOWN:
	    /* Dump only once candidates exist: most functions have none,
	       and a per-function analysis line would drown the useful
	       output.  */
	    admits_runtime_trips = true;
	    break;
	  }

      /* Lift peel-class programming across enclosing loops.  A loop that needs
	 the peel sits inside loops whose region scans refuse its CC writes, so
	 the normal placement cannot lift it and the peel and programming
	 re-execute on every outer iteration.  For the programming alone those
	 CC writes do not matter: the staged SFPLOADI + SFPCONFIG run once
	 before the outer loops, and no CC write affects a constant register.
	 Walk the enclosing loops in cc-immaterial mode, then require that no
	 function-local CC write reaches the lifted preheader.  The staged load
	 writes every lane, a superset of any consumer's mask; post-CC
	 candidates also pass the consumer audit.  On success the candidates
	 are programmed at the lifted entry and no peel is created; otherwise
	 they keep the peel placement.  */
      bool cc_lifted_loop = false;
      edge lifted_entry = entry;
      if (peel
	  && riscv_tt_opt_crossloop_cc_peel > 0
	  && riscv_tt_opt_crossloop_hoist > 0)
	{
	  lifted_entry
	    = rvtt_crossloop_outermost_entry (loop, entry, 0x7fff,
					      /*cc_immaterial=*/true);
	  if (lifted_entry != entry)
	    {
	      if (classify_second_trip (loop, entry) == TRIPS_PROVEN_SINGLE)
		{
		  rvtt_refuse (RVTT_REF_TRIP_COUNT_SINGLE_TRIP, dump_file,
			       "const-residency: loop bb %d cc-peel lift "
			       "refused (trip-count-single-trip: the loop "
			       "provably runs one trip; the lifted "
			       "programming can never recover its cost)\n",
			       loop->header->index);
		  lifted_entry = entry;
		}
	      else
		{
		  auto_vec<gimple *> lift_writers;
		  collect_cc_writers (fn, &lift_writers);
		  if (cc_write_reaches_point_p (lift_writers,
						lifted_entry->src, nullptr))
		    {
		      rvtt_refuse (RVTT_REF_CROSSLOOP_CC_PEEL_ENTRYCC_UNPROVEN,
				   dump_file,
				   "const-residency: loop bb %d cc-peel lift "
				   "refused (crossloop-cc-peel-entrycc-"
				   "unproven: a CC write reaches the lifted "
				   "preheader bb %d)\n",
				   loop->header->index,
				   lifted_entry->src->index);
		      lifted_entry = entry;
		    }
		  else
		    {
		      cc_lifted_loop = true;
		      if (dump_file)
			fprintf (dump_file,
				 "const-residency: loop bb %d cc-peel "
				 "placement lifted to entry bb %d "
				 "(programming-only lift; crossed-loop CC "
				 "immaterial to the parked constant; no "
				 "peel)\n",
				 loop->header->index,
				 lifted_entry->dest->index);
		    }
		}
	    }
	}

      /* The peel only manufactures an all-lanes programming point.  When
	 no function-local CC write reaches the loop's own preheader, the
	 plain loop class's fn-entry all-lanes proof already holds there,
	 and the body's trailing all-lanes SFPENCC carries that state into
	 every later trip, so the pre-CC candidates execute all-lanes on
	 every trip.  Program on the entry edge and skip the peel; post-CC
	 candidates still pass the consumer audit below.

	 A loop nested in another loop sees its own writers reach the
	 preheader around the enclosing backedge, but every such path
	 leaves through the body's trailing all-lanes SFPENCC.  The
	 kill-aware walk proves the same entry fact for that shape.  */
      bool own_entry = false;
      if (peel && !cc_lifted_loop
	  && classify_second_trip (loop, entry) != TRIPS_PROVEN_SINGLE)
	{
	  auto_vec<gimple *> writers;
	  collect_cc_writers (fn, &writers);
	  bool unreached
	    = !cc_write_reaches_point_p (writers, entry->src, nullptr);
	  if (unreached || block_exit_ambient_all_lanes_p (entry->src))
	    {
	      cc_lifted_loop = true;
	      own_entry = true;
	      lifted_entry = entry;
	      if (dump_file)
		fprintf (dump_file,
			 "const-residency: loop bb %d programmed on its "
			 "all-lanes entry edge (%s; no peel)\n",
			 loop->header->index,
			 unreached ? "no CC write reaches the preheader"
			 : "every CC write reaching the preheader is "
			 "killed by an all-lanes SFPENCC");
	    }
	}

      /* Placement edge for candidates that are not cc-lifted: the outermost
	 audited entry for a non-peel candidate, as in the fusion class.  A
	 peel candidate stays on the loop's own entry edge: the peel copies the
	 body onto the placement edge, so a lifted placement would seed the
	 header PHIs from an edge that does not enter the loop, move the copy
	 above the outer loops' definitions, and run the peeled iteration once
	 per function rather than once per loop entry.  The outer region scans
	 normally refuse the peel loop's CC writes anyway, but
	 -mtt-tensix-optimize-cc-region-general can admit them, so the lift is
	 refused explicitly here.  The programming-only lift above is the sound
	 way to lift a peel loop.  */
      auto plain_entry = [&] () -> edge
	{
	  if (riscv_tt_opt_crossloop_hoist <= 0)
	    return entry;
	  edge oentry = rvtt_crossloop_outermost_entry (loop, entry, 0x7fff);
	  if (peel && oentry != entry)
	    {
	      rvtt_refuse (RVTT_REF_CROSSLOOP_PEEL_ENTRY_ANCHORED, dump_file,
			   "const-residency: loop bb %d lift refused "
			   "(crossloop-peel-entry-anchored: the "
			   "first-iteration peel is anchored to the loop's "
			   "own entry edge; keeping the entry-anchored "
			   "placement)\n", loop->header->index);
	      return entry;
	    }
	  return oentry;
	};

      auto_vec<residency_candidate> this_loop;
      basic_block *body = get_loop_body_in_dom_order (loop);
      for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
	{
	  basic_block bb = body[ix];
	  if (bb->loop_father != loop
	      || !rvtt_stmt_executes_every_entered_iteration_p (loop, bb))
	    continue;
	  bool cc_reached = false;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	       gsi_next (&gsi))
	    {
	      /* Peel class: a candidate at or after the body's first CC writer
		 ran under the v_if region's partial lane mask; only the prefix
		 before it is proven all-lanes on iterations 2..N.

		 Under pressure-park such a candidate is still admitted when
		 every consumer is in the audited lane-predicated set
		 (remat_consumer_audited_p).  The constant-register read
		 carries the constant in every lane, a superset of the lanes
		 the predicated SFPLOADI wrote; the remaining lanes held an
		 undefined fresh SSA value that no audited consumer propagates.
		 A PHI use or an unaudited consumer refuses.  */
	      if (cc_limit && gsi_stmt (gsi) == cc_limit)
		cc_reached = true;
	      if (cc_reached && riscv_tt_opt_pressure_park <= 0)
		break;
	      gcall *load = dyn_cast <gcall *> (gsi_stmt (gsi));
	      if (!load || taken.contains (load))
		continue;
	      if (!rvtt_invariant_constant_load_p (load, loop,
						   /*allow_shortened=*/true))
		continue;
	      remat_chain chain;
	      if (!remat_chain_p (gimple_call_lhs (load), &chain)
		  || chain.tail != load)
		continue;
	      unsigned value;
	      if (!constant_chain_value_p (chain, &value))
		continue;
	      bool lift_this = cc_lifted_loop;
	      if (cc_reached || (cc_lifted_loop && !own_entry))
		{
		  /* The consumer audit.  Post-CC candidates always need it.  A
		     cc-lifted candidate needs it in the pre-CC prefix too:
		     without the peel the first iteration's lane state is
		     unknown (outer-loop CC code may run before the loop), so
		     the all-lanes read is exact only under the superset-write
		     argument.  A pre-CC candidate that fails the audit keeps
		     the peel placement.  */
		  const char *bad = nullptr;
		  gimple *bad_use = nullptr;
		  imm_use_iterator uit;
		  gimple *use;
		  FOR_EACH_IMM_USE_STMT (use, uit, gimple_call_lhs (load))
		    {
		      if (is_gimple_debug (use))
			continue;
		      if (gimple_code (use) == GIMPLE_PHI)
			{
			  bad = "postcc-phi-use";
			  bad_use = use;
			  break;
			}
		      if (!remat_consumer_audited_p (use,
						     gimple_call_lhs (load))
			  && !compare_vd_use_p (use, gimple_call_lhs (load)))
			{
			  bad = "consumer-lane-discipline-unaudited";
			  bad_use = use;
			  break;
			}
		    }
		  if (bad && cc_reached)
		    {
		      if (dump_file)
			{
			  rvtt_refuse_by_name (bad, dump_file,
					       "pressure-park: refused (%s): ",
					       bad);
			  print_gimple_stmt (dump_file, bad_use, 0);
			}
		      continue;
		    }
		  if (bad)
		    {
		      /* Pre-CC candidate failing the audit: keep the peel
			 placement.  */
		      lift_this = false;
		      if (dump_file)
			{
			  rvtt_refuse
			    (RVTT_REF_CROSSLOOP_CC_PEEL_CONSUMER_UNAUDITED,
			     dump_file,
			     "const-residency: cc-peel lift refused "
			     "(crossloop-cc-peel-consumer-unaudited; "
			     "candidate keeps the peel placement): ");
			  print_gimple_stmt (dump_file, bad_use, 0);
			}
		    }
		  else if (cc_reached && dump_file)
		    {
		      fprintf (dump_file,
			       "pressure-park: admitted post-CC candidate "
			       "(every consumer lane-predicated-audited): ");
		      print_gimple_stmt (dump_file, load, 0);
		    }
		}
	      residency_candidate c;
	      c.load = load;
	      c.value = value;
	      c.loop = loop;
	      if (lift_this)
		{
		  /* Programming-only lift, proven at discovery; no peel.  */
		  c.entry = lifted_entry;
		  c.peel = false;
		  c.cc_lifted = true;
		  c.own_entry = own_entry;
		}
	      else
		{
		  /* Outermost audited placement, as in the fusion class; a
		     peel candidate stays on its own entry edge (see
		     plain_entry).  */
		  c.entry = plain_entry ();
		  c.peel = peel;
		}
	      c.uses = count_nondebug_uses (gimple_call_lhs (load));
	      this_loop.safe_push (c);
	    }
	}

      /* MAD-PAIR class: the invariant pass may hoist a loop's constants into
	 the preheader, where neither the in-loop scan above nor the fusion
	 class sees them.  The downstream muli/addi immediate folds then
	 consume the shortened FLOATB materializations ahead of mul,add->mad,
	 and the loop body is left with a MUL and an ADDI per iteration.
	 Moving the fold-vulnerable hoisted constants of a single-use mul+add
	 pair into PRGM registers stops the immediate folds from matching, so
	 the existing mad combine fuses the pair.  This class only recognizes
	 the pair; whether it fuses is decided downstream.  Programming is in
	 place at the hoisted materialization, as for the PRESSURE class.  Each
	 claim costs one extra pushed word (the SFPCONFIG) against one word
	 saved per iteration, so a loop proven to run a single trip is
	 refused.  */
      {
	loop_trip_class mp_trips = !peel
	  ? (admits_runtime_trips ? TRIPS_UNKNOWN : TRIPS_AT_LEAST_2)
	  : classify_second_trip (loop, entry);
	if (mp_trips == TRIPS_PROVEN_SINGLE)
	  {
	    rvtt_refuse (RVTT_REF_TRIP_COUNT_SINGLE_TRIP, dump_file,
			 "const-residency: madpair loop bb %d refused "
			 "(trip-count-single-trip: the loop provably runs "
			 "one trip; the re-claim can never recover its "
			 "programming word)\n", loop->header->index);
	  }
	else
	  for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
	    {
	      basic_block bb = body[ix];
	      if (bb->loop_father != loop
		  || !rvtt_stmt_executes_every_entered_iteration_p (loop, bb))
		continue;
	      for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
		   !gsi_end_p (gsi); gsi_next (&gsi))
		{
		  gcall *add = dyn_cast <gcall *> (gsi_stmt (gsi));
		  if (!add)
		    continue;
		  const rvtt_insn_data *addd = rvtt_get_insn_data (add);
		  /* With -mtt-tensix-optimize-madpair-vocabulary the _lv form
		     of the add is recognized too.  */
		  bool add_lv = riscv_tt_opt_madpair_vocabulary > 0
		    && addd && addd->id == rvtt_insn_data::sfpadd_lv;
		  if (!addd
		      || (addd->id != rvtt_insn_data::sfpadd && !add_lv)
		      || !gimple_call_lhs (add)
		      || TREE_CODE (gimple_call_lhs (add)) != SSA_NAME)
		    continue;
		  unsigned ab = add_lv ? madpair_value_base (addd) : 0;
		  if (!integer_zerop (gimple_call_arg (add, ab + 2)))
		    continue;
		  for (unsigned swap = 0; swap != 2; ++swap)
		    {
		      unsigned mb;
		      gcall *mul
			= madpair_vocab_mul_p (gimple_call_arg
					       (add, ab + swap), loop,
					       add, &mb);
		      if (!mul)
			continue;
		      /* Classify the pair's three value operands.  */
		      struct pair_op
		      {
			gcall *load;
			unsigned value;
			bool vulnerable;
			bool shared;
		      };
		      pair_op ops[3];
		      unsigned nops = 0;
		      unsigned value;
		      bool vulnerable, is_shared;
		      /* A sign-flipping copy of the addend (the y * (C - x * y)
			 Newton spelling) is folded into the add's mode by the
			 combiner, so the constant behind it still feeds the
			 pair and is just as fold-vulnerable.  */
		      tree addend = gimple_call_arg (add, ab + (1 - swap));
		      gimple *addend_use = add;
		      gcall *neg = TREE_CODE (addend) == SSA_NAME
			? dyn_cast <gcall *> (SSA_NAME_DEF_STMT (addend))
			: nullptr;
		      const rvtt_insn_data *negd
			= neg ? rvtt_get_insn_data (neg) : nullptr;
		      if (negd && negd->id == rvtt_insn_data::sfpmov
			  && integer_onep (gimple_call_arg (neg, 1))
			  && gimple_bb (neg)
			  && flow_bb_inside_loop_p (loop, gimple_bb (neg))
			  && single_nondebug_use_p (addend, add))
			{
			  addend = gimple_call_arg (neg, 0);
			  addend_use = neg;
			}
		      else
			neg = nullptr;
		      if (gcall *l
			  = hoisted_madpair_load_p (addend, loop, addend_use,
						    &value, &vulnerable,
						    &is_shared))
			{
			  /* The negated-immediate fold ends at the add, not
			     at the copy.  */
			  if (neg)
			    vulnerable
			      = rvtt_combine_will_fuse_p
				  (l, rvtt_insn_data::sfploadi,
				   rvtt_insn_data::sfpadd_lv);
			  ops[nops++]
			    = pair_op { l, value, vulnerable, is_shared };
			}
		      for (unsigned mx = 0; mx != 2; ++mx)
			if (gcall *l
			    = hoisted_madpair_load_p (gimple_call_arg
						      (mul, mb + mx),
						      loop, mul, &value,
						      &vulnerable,
						      &is_shared))
			  ops[nops++]
			    = pair_op { l, value, vulnerable, is_shared };
		      /* Nothing fold-vulnerable: either no decay exists
			 (register operands fuse as they are) or nothing
			 here can prevent it.  */
		      bool any_vulnerable = false;
		      bool blocked = false;
		      for (unsigned ox = 0; ox != nops; ++ox)
			{
			  if (!ops[ox].vulnerable)
			    continue;
			  any_vulnerable = true;
			  if (ops[ox].shared
			      || taken.contains (ops[ox].load))
			    blocked = true;
			}
		      if (!any_vulnerable)
			continue;
		      if (blocked)
			{
			  rvtt_refuse (RVTT_REF_MADPAIR_SHARED_CONSTANT,
				       dump_file,
				       "const-residency: madpair loop bb %d "
				       "refused (madpair-shared-constant): a "
				       "fold-vulnerable materialization has "
				       "consumers beyond the pair; the "
				       "immediate fold fires on it regardless "
				       "of other claims\n",
				       loop->header->index);
			  break;
			}
		      /* Pair-atomic admission: every fold-vulnerable
			 constant of the pair claims under one group
			 key (all-or-none at placement).  */
		      ++madpair_group;
		      for (unsigned ox = 0; ox != nops; ++ox)
			{
			  if (!ops[ox].vulnerable)
			    continue;
			  residency_candidate c;
			  c.load = ops[ox].load;
			  c.value = ops[ox].value;
			  c.loop = loop;
			  c.entry = entry;
			  c.uses = count_nondebug_uses
			    (gimple_call_lhs (ops[ox].load));
			  c.inplace = true;
			  c.group = madpair_group;
			  madpair_cands.safe_push (c);
			  taken.add (c.load);
			  if (dump_file)
			    fprintf (dump_file,
				     "const-residency: madpair loop bb %d "
				     "candidate: hoisted constant 0x%08x is "
				     "fold-vulnerable; re-claiming would "
				     "re-offer the mul+add pair to the mad "
				     "combine\n",
				     loop->header->index, c.value);
			}
		      break;
		    }
		}
	    }
      }

      /* HOISTED-REUSE class (-mtt-tensix-optimize-hoisted-prgm-reuse): a
	 constant the invariant pass hoisted out of the loop holds an LREG for
	 the whole loop, which the downstream cross-row pairing cannot free
	 when the loop is at full pressure.  Move it into a PRGM register
	 through place(): a free register, or one whose every TU write stores
	 this same 32-bit value (reprogramming is then idempotent).
	 Programming is in place at the materialization, with the
	 PRESSURE-class reach test below; the read keeps the SSA name, so every
	 consumer sees the same all-lanes value.  Each claim costs one pushed
	 word (the SFPCONFIG); a loop proven to run a single trip is
	 refused.  */
      if (riscv_tt_opt_hoisted_prgm_reuse > 0)
	{
	  loop_trip_class hr_trips = !peel
	    ? (admits_runtime_trips ? TRIPS_UNKNOWN : TRIPS_AT_LEAST_2)
	    : classify_second_trip (loop, entry);
	  if (hr_trips == TRIPS_PROVEN_SINGLE)
	    {
	      rvtt_refuse (RVTT_REF_TRIP_COUNT_SINGLE_TRIP, dump_file,
			   "const-residency: hoisted-reuse loop bb %d refused "
			   "(trip-count-single-trip: the loop provably runs "
			   "one trip; the re-claim can never recover its "
			   "programming word)\n", loop->header->index);
	    }
	  else
	    {
	      /* Loop half of the call-free window (see call_free_window): a
		 foreign call or asm in the body rules out a reclaim for every
		 candidate of this loop.  The per-candidate half is checked
		 below.  */
	      bool loop_call_free = true;
	      basic_block *hr_body = get_loop_body_in_dom_order (loop);
	      for (unsigned ix = 0;
		   ix != loop->num_nodes && loop_call_free; ++ix)
		{
		  basic_block bb = hr_body[ix];
		  if (bb->loop_father != loop)
		    continue;
		  for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
		       !gsi_end_p (gsi) && loop_call_free; gsi_next (&gsi))
		    {
		      gimple *stmt = gsi_stmt (gsi);
		      if (is_gimple_debug (stmt))
			continue;
		      if (gimple_code (stmt) == GIMPLE_ASM
			  || (is_gimple_call (stmt)
			      && !rvtt_get_insn_data (stmt)))
			loop_call_free = false;
		    }
		}
	      for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
		{
		  basic_block bb = hr_body[ix];
		  if (bb->loop_father != loop)
		    continue;
		  for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
		       !gsi_end_p (gsi); gsi_next (&gsi))
		    {
		      gcall *use_call = dyn_cast <gcall *> (gsi_stmt (gsi));
		      if (!use_call || !rvtt_get_insn_data (use_call))
			continue;
		      for (unsigned ax = 0;
			   ax != gimple_call_num_args (use_call); ++ax)
			{
			  tree arg = gimple_call_arg (use_call, ax);
			  if (TREE_CODE (arg) != SSA_NAME)
			    continue;
			  gcall *load
			    = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (arg));
			  if (!load || !gimple_bb (load)
			      || flow_bb_inside_loop_p (loop,
							gimple_bb (load))
			      || taken.contains (load)
			      || !rvtt_invariant_constant_load_p
				   (load, loop, /*allow_shortened=*/true))
			    continue;
			  unsigned value;
			  remat_chain chain;
			  if (!remat_chain_p (arg, &chain)
			      || chain.tail != load
			      || !constant_chain_value_p (chain, &value))
			    continue;
			  residency_candidate c;
			  c.load = load;
			  c.value = value;
			  c.loop = loop;
			  c.entry = entry;
			  c.uses = count_nondebug_uses
			    (gimple_call_lhs (load));
			  c.inplace = true;
			  c.hoisted_reuse = true;
			  /* Per-candidate half of the reclaim window:
			     the load sits in the loop's entry block
			     and nothing foreign follows it there.  */
			  c.call_free_window = loop_call_free
			    && entry && gimple_bb (load) == entry->src;
			  if (c.call_free_window)
			    for (gimple_stmt_iterator wgsi
				   = gsi_for_stmt (load);
				 !gsi_end_p (wgsi); gsi_next (&wgsi))
			      {
				gimple *wstmt = gsi_stmt (wgsi);
				if (is_gimple_debug (wstmt))
				  continue;
				if (gimple_code (wstmt) == GIMPLE_ASM
				    || (is_gimple_call (wstmt)
					&& !rvtt_get_insn_data (wstmt)))
				  {
				    c.call_free_window = false;
				    break;
				  }
			      }
			  hoistreuse_cands.safe_push (c);
			  taken.add (load);
			  if (dump_file)
			    {
			      fprintf (dump_file,
				       "const-residency: hoisted-reuse loop "
				       "bb %d candidate: out-of-loop "
				       "constant 0x%08x (%u uses); "
				       "re-claiming releases its loop-wide "
				       "LREG live range: ",
				       loop->header->index, c.value, c.uses);
			      print_gimple_stmt (dump_file, load, 0);
			    }
			}
		    }
		}
	      free (hr_body);
	    }
	}
      free (body);

      if (admits_runtime_trips && !this_loop.is_empty () && dump_file)
	fprintf (dump_file,
		 "const-residency: loop bb %d admits runtime trips "
		 "(entry-edge programming is never speculated; "
		 "establishment and no-clobber are trip-independent; "
		 "worst case one extra pushed word per candidate on "
		 "a single-trip entry)\n", loop->header->index);

      /* Peel pricing (residency-peel model, rvtt-cost.md): the loop saves the
	 candidates' materialization words on every iteration after the first;
	 the programming costs one push per staged word and per SFPCONFIG, and
	 the peeled body is delivered as pushed rather than replayed words.
	 The required trip count is proven by bounded evaluation of the loop's
	 scalar control.  */
      if (peel && !this_loop.is_empty ())
	{
	  /* Price only the candidates that still need the peel; cc-lifted
	     candidates are programmed once at the lifted preheader.  */
	  unsigned sum_w = 0;
	  unsigned nprog = 0;
	  for (residency_candidate &c : this_loop)
	    if (c.peel)
	      {
		sum_w += loadi_issue_words (c.load);
		++nprog;
	      }
	  if (nprog)
	    {
	  unsigned body_w = 0;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (loop->header);
	       !gsi_end_p (gsi); gsi_next (&gsi))
	    {
	      gimple *stmt = gsi_stmt (gsi);
	      const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
	      if (!insnd)
		body_w += gimple_code (stmt) == GIMPLE_ASM;
	      else if (insnd->id == rvtt_insn_data::sfpxloadi
		       || insnd->id == rvtt_insn_data::sfploadi)
		body_w += loadi_issue_words (as_a <gcall *> (stmt));
	      else
		++body_w;
	    }
	  /* Break-even trip count (rvtt-delivery-cost-core.h).  */
	  unsigned need = rvtt_delivery_cost::residency_peel_break_even_trips
	    (rvtt_dcost_table (), sum_w, nprog, body_w);
	  /* 64 bounds the work of the trip proof, not the benefit model; a
	     break-even beyond it is refused.  */
	  if (need > 64 || !loop_trips_at_least_p (loop, entry, need))
	    {
	      rvtt_refuse (RVTT_REF_PEEL_TRIP_COUNT_UNPROVEN, dump_file,
			   "const-residency: loop bb %d refused "
			   "(peel-trip-count-unproven: break-even needs %u "
			   "proven trips; %u candidate words, %u programming "
			   "words, %u-word body)\n",
			   loop->header->index, need, sum_w, sum_w + nprog,
			   body_w);
	      /* Drop the peel candidates.  A cc-lifted candidate keeps its
		 lifted placement, which does not depend on the peel
		 break-even.  */
	      unsigned kept = 0;
	      for (residency_candidate &c : this_loop)
		if (!c.peel)
		  this_loop[kept++] = c;
	      this_loop.truncate (kept);
	    }
	  else if (dump_file)
	    fprintf (dump_file,
		     "const-residency: loop bb %d admits the CC-canonical "
		     "peel (%u candidate words/iteration, break-even %u "
		     "trips proven)\n",
		     loop->header->index, sum_w, need);
	    }
	}

      /* Call-free window for -mtt-tensix-optimize-loop-prgm-reclaim.
	 Reclaiming a claimed but unread register means another TU writer
	 stores a different value there, so no call may run between this
	 function's programming and its in-loop reads.  For the loop
	 programming point the window is the loop body (the peeled copy is a
	 body copy) plus the entry edge.  A crossloop- or cc-lifted entry
	 widens the window over code not checked here, so those candidates
	 cannot reclaim; free and same-value registers are unaffected.  An
	 own-entry candidate uses the loop's own entry and keeps the proof.  */
      if (riscv_tt_opt_loop_prgm_reclaim > 0 && !this_loop.is_empty ())
	{
	  bool loop_call_free = true;
	  basic_block *lr_body = get_loop_body_in_dom_order (loop);
	  for (unsigned ix = 0;
	       ix != loop->num_nodes && loop_call_free; ++ix)
	    {
	      basic_block bb = lr_body[ix];
	      for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
		   !gsi_end_p (gsi) && loop_call_free; gsi_next (&gsi))
		{
		  gimple *stmt = gsi_stmt (gsi);
		  if (is_gimple_debug (stmt))
		    continue;
		  if (gimple_code (stmt) == GIMPLE_ASM
		      || (is_gimple_call (stmt)
			  && !rvtt_get_insn_data (stmt)))
		    loop_call_free = false;
		}
	    }
	  free (lr_body);
	  for (residency_candidate &c : this_loop)
	    c.call_free_window
	      = (loop_call_free && (!c.cc_lifted || c.own_entry)
		 && c.entry == entry);
	  if (!loop_call_free && dump_file)
	    fprintf (dump_file,
		     "const-residency: loop bb %d reclaim window unproven "
		     "(loop-reclaim-call-window: foreign call or asm in "
		     "the loop body)\n", loop->header->index);
	}
      for (residency_candidate &c : this_loop)
	{
	  loop_cands.safe_push (c);
	  taken.add (c.load);
	}
    }
}

/* PRESSURE class candidate discovery: when the pressure model
   exceeds the LREG file, collect FN's pressure-class candidates into
   PRESSURE_CANDS (TAKEN marks claimed statements).  */

static void
residency_collect_pressure_class (function *fn,
				  auto_vec<residency_candidate> &pressure_cands,
				  hash_set<gimple *> &taken)
{
  {
    const unsigned capacity = rvtt_pressure_capacity ();
    rvtt_pressure_model model;
    rvtt_pressure_compute (fn, capacity, &model);
    if (model.peak > capacity)
      {
	unsigned version;
	tree name;
	FOR_EACH_SSA_NAME (version, name, fn)
	  {
	    remat_chain chain;
	    if (!rvtt_pressure_tracked_p (name)
		|| !remat_chain_p (name, &chain))
	      continue;
	    /* A two-instruction chain is still one constant; the value
	       derivation handles the pair and the dead root falls to DCE.  */
	    if (taken.contains (chain.tail))
	      continue;
	    unsigned value;
	    if (!constant_chain_value_p (chain, &value))
	      continue;
	    /* Out-of-loop definitions only: replacing an in-loop
	       definition in place would reprogram every iteration.  */
	    basic_block def_bb = gimple_bb (chain.tail);
	    if (def_bb->loop_father && def_bb->loop_father->num != 0)
	      continue;
	    bool relevant = bitmap_bit_p (model.over_bbs, def_bb->index);
	    if (!relevant)
	      {
		bitmap_iterator bi;
		unsigned bbi;
		EXECUTE_IF_SET_IN_BITMAP (model.over_bbs, 0, bbi, bi)
		  if (bbi < model.live_in.length () && model.live_in[bbi]
		      && bitmap_bit_p (model.live_in[bbi],
				       SSA_NAME_VERSION (name)))
		    {
		      relevant = true;
		      break;
		    }
	      }
	    if (!relevant)
	      continue;
	    residency_candidate c;
	    c.load = chain.tail;
	    c.value = value;
	    c.loop = nullptr;
	    c.entry = nullptr;
	    c.uses = count_nondebug_uses (name);
	    pressure_cands.safe_push (c);
	    taken.add (chain.tail);
	  }
      }
    else if (dump_file)
      fprintf (dump_file, "const-residency: pressure %u within the %u-LREG "
	       "file; pressure class idle\n", model.peak, capacity);
  }
}

/* Replace the constant materialization LOAD with a read of PRGM
   register REG.  The read keeps LOAD's SSA name, so every use follows
   untouched; it expands to a zero-pressure cstlreg unspec.  */

static void
replace_with_prgm_read (gcall *load, unsigned reg)
{
  const rvtt_insn_data *readlreg_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);
  gcall *chain_root = rvtt_chained_loadi_root (load);
  gimple_stmt_iterator lgsi = gsi_for_stmt (load);
  gcall *read = gimple_build_call
    (readlreg_d->decl, 1, build_int_cst (unsigned_type_node, reg));
  gimple_call_set_lhs (read, gimple_call_lhs (load));
  unlink_stmt_vdef (load);
  if (tree vdef = gimple_vdef (load))
    {
      gimple_set_vdef (load, NULL_TREE);
      if (TREE_CODE (vdef) == SSA_NAME)
	release_ssa_name (vdef);
    }
  gsi_replace (&lgsi, read, false);
  /* Immediate lowering split this value into a root and tail.  The
     read replaces the whole materialization, so retaining the now-dead
     root would still emit an architectural SFPLOADI side effect.  */
  if (chain_root)
    {
      gimple_stmt_iterator rgsi = gsi_for_stmt (chain_root);
      if (tree vdef = gimple_vdef (chain_root))
	if (TREE_CODE (vdef) == SSA_NAME)
	  unlink_stmt_vdef (chain_root);
      gsi_remove (&rgsi, true);
      release_defs (chain_root);
    }
}

/* REPEAT class (-mtt-tensix-optimize-repeat-residency).

   A row loop written under `#pragma GCC unroll' inside a noinline body is
   straight-line code by the time this pass runs, and every row materializes
   its coefficients again: two pushed SFPLOADI words per wide constant per row.
   There is no loop for the LOOP class to hoist out of, and the pressure class
   only rewrites in place.

   For a function with no loop around the materializations, no call and no asm,
   this class groups the out-of-loop materializations by their 32-bit lane
   image and, for a group whose words saved exceed the programming cost,
   programs a PRGM register once at function entry and turns every member into
   a read of it.

   Exactness:
   - the programming point is the function entry, whose lane state is the
     all-lanes ambient every class here assumes, so the register holds the
     image in every lane;
   - a member that executed with all lanes enabled (its nearest preceding CC
     event in the block is the all-lanes SFPENCC, or the block entry is proven
     all-lanes) wrote the image to every lane, so the read is identical.  A
     member under a partial mask is admitted only when every consumer passes
     the lane-discipline audit (as in the pressure-park tier); otherwise it
     keeps its own materialization;
   - nothing can rewrite the register between entry and a member: the function
     has no call and no asm, a free register has no writer in the TU, and a
     reclaimed register (claimed but read by no TU statement) has its writers
     outside this function.

   Members feeding a compare's VD, a LUT slot, a PHI or a store (SFPSTORE
   cannot source a PRGM register) are left alone, as in the other classes.  */

struct repeat_group
{
  unsigned value;
  auto_vec<gcall *> members;
  int saved;			/* issue words saved per call */
};

/* Return true if all lanes are enabled when STMT executes: the nearest
   preceding typed CC event in its block is the all-lanes SFPENCC, or there is
   none and the block entry is proven all-lanes.  */

static bool
stmt_ambient_all_lanes_p (gimple *stmt)
{
  gimple_stmt_iterator gsi = gsi_for_stmt (stmt);
  for (gsi_prev (&gsi); !gsi_end_p (gsi); gsi_prev (&gsi))
    {
      gimple *s = gsi_stmt (gsi);
      if (is_gimple_debug (s))
	continue;
      const rvtt_insn_data *insnd = rvtt_get_insn_data (s);
      if (!insnd)
	continue;
      if (rvtt_all_lanes_encc_p (s))
	return true;
      gcall *call = as_a <gcall *> (s);
      if (insnd->sets_cc (call)
	  || insnd->id == rvtt_insn_data::sfppushc
	  || insnd->id == rvtt_insn_data::sfppopc)
	return false;
    }
  return prepeel_ambient_all_lanes_p (gimple_bb (stmt));
}

/* REPEAT class discovery: collect FN's profitable value groups into GROUPS,
   most words saved first.  TAKEN marks claimed statements.  */

static void
residency_collect_repeat_class (function *fn,
				auto_vec<repeat_group *> &groups,
				hash_set<gimple *> &taken)
{
  if (riscv_tt_opt_repeat_residency <= 0)
    return;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (gimple_code (stmt) == GIMPLE_ASM
	    || (is_gimple_call (stmt) && !rvtt_get_insn_data (stmt)))
	  {
	    if (dump_file)
	      fprintf (dump_file, "const-residency: repeat class refused "
		       "(the function has a call or asm)\n");
	    return;
	  }
      }

  hash_map<int_hash<unsigned, 0>, unsigned> by_value;
  auto_vec<repeat_group *> all;
  FOR_EACH_BB_FN (bb, fn)
    {
      if (bb->loop_father && bb->loop_father->num != 0)
	continue;
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gcall *load = dyn_cast <gcall *> (gsi_stmt (gsi));
	  if (!load || taken.contains (load) || !gimple_call_lhs (load))
	    continue;
	  tree name = gimple_call_lhs (load);
	  remat_chain chain;
	  if (!remat_chain_p (name, &chain) || chain.tail != load)
	    continue;
	  unsigned value;
	  if (!constant_chain_value_p (chain, &value) || value == 0)
	    continue;
	  bool all_lanes = stmt_ambient_all_lanes_p (load);
	  bool ok = !rvtt_pressure_lut_slot_use_p (name);
	  imm_use_iterator uit;
	  gimple *use;
	  FOR_EACH_IMM_USE_STMT (use, uit, name)
	    {
	      if (!ok || is_gimple_debug (use))
		continue;
	      const rvtt_insn_data *uinsnd = rvtt_get_insn_data (use);
	      if (gimple_code (use) == GIMPLE_PHI
		  || compare_vd_use_p (use, name)
		  || (uinsnd && uinsnd->id == rvtt_insn_data::sfpstore)
		  || (!all_lanes && !remat_consumer_audited_p (use, name)))
		ok = false;
	    }
	  if (!ok)
	    continue;
	  unsigned *slot = by_value.get (value);
	  repeat_group *g;
	  if (slot)
	    g = all[*slot];
	  else
	    {
	      g = new repeat_group;
	      g->value = value;
	      g->saved = 0;
	      by_value.put (value, all.length ());
	      all.safe_push (g);
	    }
	  g->members.safe_push (load);
	  g->saved += loadi_issue_words (load);
	}
    }

  for (repeat_group *g : all)
    {
      /* Programming costs the staged image plus the SFPCONFIG.  */
      if (!g->members.is_empty ())
	g->saved -= 1 + (int) loadi_issue_words (g->members[0]);
      if (g->members.length () < 2 || g->saved <= 0)
	{
	  delete g;
	  continue;
	}
      for (gcall *m : g->members)
	taken.add (m);
      groups.safe_push (g);
    }
  /* Most words saved first; the image breaks ties deterministically.  */
  for (unsigned i = 1; i < groups.length (); ++i)
    for (unsigned j = i; j > 0
	 && (groups[j - 1]->saved < groups[j]->saved
	     || (groups[j - 1]->saved == groups[j]->saved
		 && groups[j - 1]->value > groups[j]->value)); --j)
      std::swap (groups[j - 1], groups[j]);
}

/* Constant-residency stage of the pass: collect the candidates of each class
   over FN, apply each class's legality checks and place the survivors.  ST
   carries the pass-wide state.  Return true if any statement changed.  */

bool
residency_transform (function *fn, prgm_state *st)
{
  if (!dom_info_available_p (CDI_DOMINATORS))
    calculate_dominance_info (CDI_DOMINATORS);

  auto_vec<residency_candidate> loop_cands;
  auto_vec<residency_candidate> pressure_cands;
  auto_vec<residency_candidate> madpair_cands;
  auto_vec<residency_candidate> hoistreuse_cands;
  unsigned madpair_group = 0;
  hash_set<int_hash<unsigned, 0> > invalid_madpair_groups;
  hash_set<gimple *> taken;

  /* LOOP class.  */
  residency_collect_loop_class (fn, loop_cands, madpair_cands,
				hoistreuse_cands, madpair_group, taken);

  /* PRESSURE class: only when the model exceeds the LREG file.  */
  residency_collect_pressure_class (fn, pressure_cands, taken);

  /* REPEAT class: straight-line repeats in a loop-free, call-free
     function.  */
  auto_vec<repeat_group *> repeat_groups;
  residency_collect_repeat_class (fn, repeat_groups, taken);
  struct repeat_groups_release
  {
    auto_vec<repeat_group *> &v;
    ~repeat_groups_release () { for (repeat_group *g : v) delete g; }
  } repeat_release { repeat_groups };

  if (loop_cands.is_empty () && pressure_cands.is_empty ()
      && madpair_cands.is_empty () && hoistreuse_cands.is_empty ()
      && repeat_groups.is_empty ())
    return false;

  /* The TU freedom proof and the all-lanes proof gate every allocation, as for
     the fusion class.  */
  const prgm_tu_facts &facts = tu_prgm_facts ();
  if (facts.refused)
    {
      rvtt_refuse (RVTT_REF_OPAQUE_REGION_UNDECLARED, dump_file,
		   "const-residency: refused (opaque-region-undeclared): %s\n",
		   facts.reason);
      return false;
    }
  {
    auto_vec<gimple *> cc_writers;
    collect_cc_writers (fn, &cc_writers);
    if (!cc_writers.is_empty ())
      {
	/* All-lanes proof by reachability: a candidate is refused when a
	   function-local CC writer can execute before its programming point
	   (the same test as the fusion class).  Peel candidates are exempt:
	   their programming follows the peeled iteration's all-lanes SFPENCC,
	   and every replaced materialization ran in that same state.  The
	   in-place classes test their own statement.  */
	unsigned kept = 0;
	for (residency_candidate &c : loop_cands)
	  {
	    /* A cc-lifted candidate was proven at discovery.  The header test
	       would wrongly count the loop's own CC writers, which run after
	       the lifted point.  */
	    if (!c.peel && !c.cc_lifted
		&& cc_write_reaches_point_p (cc_writers, c.loop->header,
					     nullptr))
	      {
		rvtt_refuse (RVTT_REF_CC_REGION_UNPROVEN, dump_file,
			     "const-residency: loop bb %d refused "
			     "(cc-region-unproven): a CC write reaches the "
			     "programming point\n", c.loop->header->index);
		continue;
	      }
	    loop_cands[kept++] = c;
	  }
	loop_cands.truncate (kept);
	kept = 0;
	for (residency_candidate &c : pressure_cands)
	  {
	    if (cc_write_reaches_point_p (cc_writers, gimple_bb (c.load),
					  c.load))
	      {
		rvtt_refuse (RVTT_REF_CC_REGION_UNPROVEN, dump_file,
			     "const-residency: pressure candidate in bb %d "
			     "refused (cc-region-unproven): a CC write reaches "
			     "the in-place programming point\n",
			     gimple_bb (c.load)->index);
		continue;
	      }
	    pressure_cands[kept++] = c;
	  }
	pressure_cands.truncate (kept);
	/* MAD-PAIR: in-place reach test.  A refused member invalidates its
	   whole group, since the remaining claims would pay for programming
	   while the surviving immediate fold still blocks the mad combine.  */
	kept = 0;
	for (residency_candidate &c : madpair_cands)
	  {
	    if (cc_write_reaches_point_p (cc_writers, gimple_bb (c.load),
					  c.load))
	      {
		rvtt_refuse (RVTT_REF_CC_REGION_UNPROVEN, dump_file,
			     "const-residency: madpair candidate in bb %d "
			     "refused (cc-region-unproven): a CC write reaches "
			     "the in-place programming point\n",
			     gimple_bb (c.load)->index);
		invalid_madpair_groups.add (c.group);
		continue;
	      }
	    madpair_cands[kept++] = c;
	  }
	madpair_cands.truncate (kept);
	/* HOISTED-REUSE class: same in-place programming point, same
	   pressure-style reach test; no grouping.  */
	kept = 0;
	for (residency_candidate &c : hoistreuse_cands)
	  {
	    if (cc_write_reaches_point_p (cc_writers, gimple_bb (c.load),
					  c.load))
	      {
		rvtt_refuse (RVTT_REF_CC_REGION_UNPROVEN, dump_file,
			     "const-residency: hoisted-reuse candidate in bb "
			     "%d refused (cc-region-unproven): a CC write "
			     "reaches the in-place programming point\n",
			     gimple_bb (c.load)->index);
		continue;
	      }
	    hoistreuse_cands[kept++] = c;
	  }
	hoistreuse_cands.truncate (kept);
	if (loop_cands.is_empty () && pressure_cands.is_empty ()
	    && madpair_cands.is_empty () && hoistreuse_cands.is_empty ()
	    && repeat_groups.is_empty ())
	  {
	    rvtt_refuse (RVTT_REF_CC_REGION_UNPROVEN, dump_file,
			 "const-residency: refused (cc-region-unproven)"
			 " -- in-function CC writes reach every candidate"
			 " programming point; cross-call ambient proof is"
			 " not on"
			 " record here\n");
	    return false;
	  }
      }
  }
  if (!st->initialized)
    {
      st->claimed = facts.claimed;
      st->initialized = true;
    }

  /* Rank by use count, then by value for determinism.  A words-saved key is
     not used because it ignores pressure: it can give a register to a
     duplicate-value pair ahead of a loop-wide candidate that relieves
     pressure, overflowing the 8-LREG file downstream.  */
  auto rank = [] (auto_vec<residency_candidate> &v)
    {
      for (unsigned i = 1; i < v.length (); ++i)
	for (unsigned j = i; j > 0
	     && (v[j - 1].uses < v[j].uses
		 || (v[j - 1].uses == v[j].uses
		     && v[j - 1].value > v[j].value)); --j)
	  std::swap (v[j - 1], v[j]);
    };
  rank (loop_cands);
  rank (pressure_cands);
  /* Priced ranking under -mtt-tensix-optimize-priced-placement.  The primary
     key is pressure relief (the candidate's block is over capacity in the
     function pressure model), the secondary key the amortized delivery benefit
     from rvtt_place_net_benefit: for the LOOP class, one in-loop word saved
     per iteration against two programming words per entry; for the in-place
     classes, the one extra programming word.  Ties fall back to the use-count
     order.  If any candidate's trip weight is unproven or the class is over
     budget, the use-count order is kept.  */
  /* Heap-held: the model's destructor releases an obstack only its
     compute initializes, so an unused model must never be
     destructed.  */
  rvtt_pressure_model *arb_model = nullptr;
  auto priced_rank = [&] (auto_vec<residency_candidate> &v, const char *cls)
    {
      if (v.length () < 2 || riscv_tt_opt_priced_placement <= 0)
	return;
      if (v.length () > RVTT_PLACE_MAX_CANDIDATES)
	{
	  rvtt_refuse (RVTT_REF_PLACE_BUDGET_EXHAUSTED, dump_file,
		       "placement-arbiter: residency-rank %s over budget "
		       "(place-budget-exhausted); the use-count order "
		       "stands\n", cls);
	  return;
	}
      struct priced_key
      {
	bool relief;
	int64_t net;
	unsigned uses;
	unsigned value;
	unsigned idx;		/* use-count-order position (stability) */
      };
      auto_vec<priced_key, 32> keys;
      for (unsigned i = 0; i < v.length (); ++i)
	{
	  residency_candidate &c = v[i];
	  rvtt_place_weight w
	    = rvtt_place_loop_weight (c.loop && !c.inplace ? c.loop
				      : nullptr);
	  if (!w.proven)
	    {
	      rvtt_refuse (RVTT_REF_PLACE_ALTERNATIVE_UNPRICEABLE,
			   dump_file,
			   "placement-arbiter: residency-rank %s "
			   "unpriceable (place-alternative-unpriceable: "
			   "trip weight unproven); the use-count order "
			   "stands\n", cls);
	      return;
	    }
	  if (!arb_model)
	    {
	      arb_model = new rvtt_pressure_model;
	      rvtt_pressure_compute (fn, rvtt_pressure_capacity (),
				     arb_model);
	    }
	  priced_key k;
	  k.relief = arb_model->over_bbs
	    && bitmap_bit_p (arb_model->over_bbs,
			     gimple_bb (c.load)->index);
	  k.net = c.loop && !c.inplace
	    ? rvtt_place_net_benefit (1, 2, w)
	    : rvtt_place_net_benefit (0, 1, w);
	  k.uses = c.uses;
	  k.value = c.value;
	  k.idx = i;
	  keys.safe_push (k);
	}
      auto before = [] (const priced_key &a, const priced_key &b)
	{
	  if (a.relief != b.relief)
	    return a.relief;
	  if (a.net != b.net)
	    return a.net > b.net;
	  if (a.uses != b.uses)
	    return a.uses > b.uses;
	  if (a.value != b.value)
	    return a.value < b.value;
	  return a.idx < b.idx;
	};
      /* The same deterministic insertion discipline as the use-count rank.  */
      for (unsigned i = 1; i < keys.length (); ++i)
	for (unsigned j = i; j > 0 && before (keys[j], keys[j - 1]); --j)
	  std::swap (keys[j - 1], keys[j]);
      bool differs = false;
      for (unsigned i = 0; i < keys.length (); ++i)
	differs |= keys[i].idx != i;
      if (dump_file)
	{
	  fprintf (dump_file, "placement-arbiter: residency-rank %s priced=[",
		   cls);
	  for (unsigned i = 0; i < keys.length (); ++i)
	    fprintf (dump_file, "%s0x%08x", i ? "," : "",
		     v[keys[i].idx].value);
	  fprintf (dump_file, "]%s\n", differs ? " (reordered)" : "");
	}
      if (differs)
	{
	  auto_vec<residency_candidate, 32> reordered;
	  for (unsigned i = 0; i < keys.length (); ++i)
	    reordered.safe_push (v[keys[i].idx]);
	  for (unsigned i = 0; i < keys.length (); ++i)
	    v[i] = reordered[i];
	}
    };
  priced_rank (loop_cands, "loop-class");
  priced_rank (pressure_cands, "pressure-class");
  /* Loop reclaim: a dead claimed register whose TU value equals some pending
     candidate's value is reserved for that candidate's same-value reuse.
     Reclaiming it for a different value would make that reuse impossible and
     can starve a candidate that relieves pressure.  */
  auto_vec<unsigned, 32> reclaim_reserved_values;
  if (riscv_tt_opt_loop_prgm_reclaim > 0)
    {
      for (residency_candidate &c : loop_cands)
	reclaim_reserved_values.safe_push (c.value);
      for (residency_candidate &c : pressure_cands)
	reclaim_reserved_values.safe_push (c.value);
      for (residency_candidate &c : madpair_cands)
	reclaim_reserved_values.safe_push (c.value);
      for (residency_candidate &c : hoistreuse_cands)
	reclaim_reserved_values.safe_push (c.value);
    }
  /* MAD-PAIR candidates stay in discovery order: members of one pair
     are contiguous and must remain so for the all-or-none placement
     sweep below.  */

  const rvtt_insn_data *wrcfg_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpwriteconfig_v);

  bool changed = false;
  /* One record per peeled loop: the post-peel entry edge (the
     programming point's home), the peel block itself, and the peel's
     original-name -> copy-name map (the park tier's pre-peel placement
     erases a candidate's duplicated materialization through it).  */
  struct peel_record
  {
    edge entry;
    basic_block copy_bb;
    hash_map<tree, tree> names;
  };
  hash_map<class loop *, peel_record *> peeled;
  auto_vec<peel_record *> peel_records;
  /* CC-canonical class: the programming point is the fall-through edge
     of the peeled first iteration (one peel per loop; later candidates
     of the same loop share it).  Peeling happens only after a placement
     decision is final, so refused candidates never mutate the CFG.  */
  auto ensure_peeled = [&] (residency_candidate &c) -> peel_record *
    {
      if (!c.peel)
	return nullptr;
      if (peel_record **found = peeled.get (c.loop))
	{
	  c.entry = (*found)->entry;
	  return *found;
	}
      peel_record *rec = new peel_record;
      rec->entry = peel_first_iteration (c.loop, c.entry, &rec->copy_bb,
					 &rec->names);
      c.entry = rec->entry;
      peeled.put (c.loop, rec);
      peel_records.safe_push (rec);
      return rec;
    };
  /* SFPSTORE sources only L0-L11 (SFPSTORE.md; SFPSTORE_MAX_SRC_LREG), so
     storing a PRGM-resident (L12-L14) constant needs an SFPMOV copy at every
     store, one word per row inside a loop.  */
  auto store_consumed_p = [] (residency_candidate &c) -> bool
    {
      imm_use_iterator uit;
      gimple *use;
      FOR_EACH_IMM_USE_STMT (use, uit, gimple_call_lhs (c.load))
	{
	  if (is_gimple_debug (use))
	    continue;
	  const rvtt_insn_data *uinsnd = rvtt_get_insn_data (use);
	  if (uinsnd && uinsnd->id == rvtt_insn_data::sfpstore
	      && gimple_call_arg (as_a <gcall *> (use), 1)
		 == gimple_call_lhs (c.load))
	    return true;
	}
      return false;
    };
  auto place = [&] (residency_candidate &c) -> bool
    {
      /* Under -mtt-tensix-optimize-store-sink the copy a store-consumed
	 constant needs would give back the word the store sink saves, so PRGM
	 placement is refused for every class and the LOOP class falls back to
	 the LREG tier.  Without it, the store-source tier below handles such
	 candidates.  */
      if (riscv_tt_opt_store_sink > 0 && store_consumed_p (c))
	{
	  if (dump_file)
	    {
	      rvtt_refuse (RVTT_REF_STORE_SOURCE_ENCODING_CEILING, dump_file,
			   "const-residency: refused "
			   "(store-source-encoding-ceiling): ");
	      print_gimple_stmt (dump_file, c.load, 0);
	    }
	  return false;
	}
      unsigned prgm = 0;
      basic_block prior_bb = nullptr;
      bool prior_reclaimed = false;
      bool prior_at_end = false;
      for (prgm_alloc &a : st->allocs)
	if (a.value == c.value)
	  {
	    prgm = a.reg;
	    prior_bb = a.bb;
	    prior_reclaimed = a.reclaimed;
	    prior_at_end = a.at_end;
	    break;
	  }
      /* A same-value candidate on a reclaimed register: another TU writer
	 stores a different value there, so the earlier programming may not
	 survive an intervening call.  The candidate needs its own call-free
	 window and always reprograms.  */
      if (prgm && prior_reclaimed && !c.call_free_window)
	{
	  if (dump_file)
	    {
	      rvtt_refuse (RVTT_REF_LOOP_RECLAIM_CALL_WINDOW, dump_file,
			   "const-residency: refused "
			   "(loop-reclaim-call-window: same-value candidate "
			   "on a reclaimed slot without its own proven "
			   "window): ");
	      print_gimple_stmt (dump_file, c.load, 0);
	    }
	  return false;
	}
      if (!prgm)
	for (unsigned reg : prgm_regs)
	  if (!(st->claimed & (1u << reg)))
	    {
	      prgm = reg;
	      break;
	    }
      /* TU same-value reuse: a claimed register whose every TU write stores
	 exactly this value may be reused.  This is sound by idempotence,
	 independent of ordering: the candidate still programs the value into
	 every lane below, and any other write, predicated or not, stores the
	 same value.  */
      bool tu_reuse = false;
      if (!prgm)
	{
	  const prgm_tu_facts &tu = tu_prgm_facts ();
	  for (unsigned reg : prgm_regs)
	    if ((tu.value_known & (1u << reg)) && tu.value[reg] == c.value)
	      {
		/* Skip a register this function already reclaimed for a
		   different value; it no longer holds the TU value.  The
		   candidate may still reclaim another dead register below.  */
		bool repurposed = false;
		for (prgm_alloc &a : st->allocs)
		  repurposed |= a.reg == reg && a.value != c.value;
		if (repurposed)
		  continue;
		prgm = reg;
		tu_reuse = true;
		break;
	      }
	}
      /* Dead-claim reclaim (hoisted-reuse and loop-reclaim candidates): a
	 claimed register that no TU statement reads may be reprogrammed with a
	 different value, provided the candidate's programming-to-reader window
	 is call-free; otherwise a callee could reprogram it in between.
	 Same-value reuse above needs no window.  */
      bool tu_reclaim = false;
      bool loop_reclaim_cand = (!c.hoisted_reuse && c.loop
				&& riscv_tt_opt_loop_prgm_reclaim > 0);
      if (!prgm
	  && ((c.hoisted_reuse && riscv_tt_opt_hoisted_prgm_reuse > 0)
	      || loop_reclaim_cand))
	{
	  const prgm_tu_facts &tu = tu_prgm_facts ();
	  unsigned dead = 0;
	  for (unsigned reg : prgm_regs)
	    {
	      if (tu.creg_read & (1u << reg))
		continue;
	      if (!(tu.claimed & (1u << reg)))
		continue;
	      /* Keep a register that is some pending candidate's same-value
		 home (see reclaim_reserved_values above).  The set is filled
		 only under -mtt-tensix-optimize-loop-prgm-reclaim.  */
	      if (tu.value_known & (1u << reg))
		{
		  bool reserved = false;
		  for (unsigned rv : reclaim_reserved_values)
		    reserved |= rv == tu.value[reg];
		  if (reserved)
		    continue;
		}
	      bool in_allocs = false;
	      for (prgm_alloc &a : st->allocs)
		in_allocs |= a.reg == reg;
	      if (!in_allocs)
		{
		  dead = reg;
		  break;
		}
	    }
	  if (dead && !c.call_free_window)
	    {
	      if (dump_file)
		{
		  rvtt_refuse_by_name (c.hoisted_reuse
				       ? "hoisted-reuse-call-window"
				       : "loop-reclaim-call-window",
				       dump_file,
				       "const-residency: refused "
				       "(%s): ",
				       c.hoisted_reuse
				       ? "hoisted-reuse-call-window"
				       : "loop-reclaim-call-window");
		  print_gimple_stmt (dump_file, c.load, 0);
		}
	      return false;
	    }
	  if (dead)
	    {
	      prgm = dead;
	      tu_reclaim = true;
	    }
	}
      if (!prgm)
	{
	  if (dump_file)
	    {
	      rvtt_refuse (RVTT_REF_PRGM_EXHAUSTED, dump_file,
			   "const-residency: refused "
			   "(prgm-exhausted): ");
	      print_gimple_stmt (dump_file, c.load, 0);
	    }
	  return false;
	}
      st->claimed |= 1u << prgm;
      if (tu_reuse && dump_file)
	fprintf (dump_file,
		 "const-residency: reusing TU-programmed PRGM L%u (every "
		 "TU write stores 0x%08x; programming is value-idempotent)\n",
		 prgm, c.value);
      if (tu_reclaim && dump_file)
	fprintf (dump_file,
		 "const-residency: reclaiming DEAD-claimed PRGM L%u for "
		 "0x%08x (%s class; no TU reader of the slot; call-free "
		 "programming-to-readers window)\n",
		 prgm, c.value,
		 c.hoisted_reuse ? "hoisted-reuse" : "loop");

      /* CC-canonical class: peel only here, after a register has
	 actually been allocated (see ensure_peeled above).  */
      ensure_peeled (c);

      basic_block point_bb = (c.loop && !c.inplace)
	? c.entry->dest : gimple_bb (c.load);
      /* Reusing a register without reprogramming requires the earlier
	 programming to execute first.  Dominance is reflexive and an in-place
	 programming point does not dominate later statements of its own block,
	 so the same block reprograms (redundant but sound).  A LOOP-class
	 programming sits at the end of its preheader and covers every later
	 candidate whose reads are in blocks that preheader strictly dominates,
	 including other candidates of the same loop.  */
      basic_block use_bb = (c.loop && !c.inplace)
	? c.loop->header : gimple_bb (c.load);
      bool reprogram
	= !prior_bb
	  || prior_reclaimed
	  || (prior_at_end
	      ? (use_bb == prior_bb
		 || !dominated_by_p (CDI_DOMINATORS, use_bb, prior_bb))
	      : (point_bb == prior_bb
		 || !dominated_by_p (CDI_DOMINATORS, point_bb, prior_bb)));
      if (reprogram)
	{
	  auto_vec<gcall *> seq;
	  tree staged = rvtt_build_loadimm32 (c.value, &seq);
	  gcall *wrcfg = gimple_build_call
	    (wrcfg_d->decl, 3, staged, build_int_cst (unsigned_type_node, 0),
	     build_int_cst (unsigned_type_node, prgm));
	  basic_block prog_bb = point_bb;
	  bool prog_at_end = false;
	  if (c.loop && !c.inplace)
	    {
	      basic_block preheader = rvtt_commit_hoist_preheader (c.entry);
	      prog_bb = preheader;
	      prog_at_end = true;
	      gimple_stmt_iterator phg = gsi_last_bb (preheader);
	      if (gsi_end_p (phg) || !stmt_ends_bb_p (gsi_stmt (phg)))
		{
		  gsi_insert_after (&phg, wrcfg, GSI_NEW_STMT);
		  for (gcall *ld : seq)
		    gsi_insert_before (&phg, ld, GSI_SAME_STMT);
		}
	      else
		{
		  /* GSI_SAME_STMT keeps the iterator on the block
		     terminator: insert the definition first so the
		     SFPCONFIG lands after its staged operand.  */
		  for (gcall *ld : seq)
		    gsi_insert_before (&phg, ld, GSI_SAME_STMT);
		  gsi_insert_before (&phg, wrcfg, GSI_SAME_STMT);
		}
	    }
	  else
	    {
	      gimple_stmt_iterator lgsi = gsi_for_stmt (c.load);
	      for (gcall *ld : seq)
		gsi_insert_before (&lgsi, ld, GSI_SAME_STMT);
	      gsi_insert_before (&lgsi, wrcfg, GSI_SAME_STMT);
	    }
	  if (!prior_bb)
	    st->allocs.safe_push (prgm_alloc { c.value, prgm, prog_bb,
					       tu_reclaim, prog_at_end });
	}
      else if (dump_file)
	fprintf (dump_file,
		 "const-residency: reused PRGM L%u for identical constant "
		 "0x%08x (dominating programming point bb %d)\n",
		 prgm, c.value, prior_bb->index);

      /* The materialization becomes a constant-register read keeping
	 its SSA name.  */
      replace_with_prgm_read (c.load, prgm);

      if (dump_file && reprogram)
	fprintf (dump_file,
		 "const-residency: allocated PRGM L%u for constant 0x%08x "
		 "(%s class, %u uses, programming point bb %d)\n",
		 prgm, c.value,
		 c.hoisted_reuse ? "hoisted-reuse"
		 : c.inplace ? "madpair" : c.loop ? "loop" : "pressure",
		 c.uses, point_bb->index);
      return true;
    };

  /* Pressure-park LREG tier (-mtt-tensix-optimize-pressure-park): a LOOP
     candidate that gets no PRGM register may still be hoisted out of the loop
     as a plain LREG live range.  The budget is the function's SSA pressure
     model (rvtt_pressure_residual); each hoist charges one register and
     exhaustion refuses.  The placement is valid for the same reason as the
     programming point: for the peel class the hoisted SFPLOADI follows the
     peeled all-lanes SFPENCC and writes every lane (post-CC candidates passed
     the consumer audit at collection); for the plain LOOP class no
     function-local CC write reaches the entry edge, so the preheader lane
     state equals the in-loop lane state.  */
  int lreg_budget = -1;		/* computed lazily on first exhaustion */
  auto lreg_hoist = [&] (residency_candidate &c) -> bool
    {
      if (riscv_tt_opt_pressure_park <= 0 || !c.loop || c.inplace)
	return false;
      if (lreg_budget < 0)
	{
	  lreg_budget = rvtt_pressure_residual (fn);
	}
      if (lreg_budget < 1)
	{
	  if (dump_file)
	    {
	      rvtt_refuse (RVTT_REF_LREG_FILE_EXHAUSTED, dump_file,
			   "pressure-park: refused (lreg-file-exhausted): the "
			   "function pressure model leaves no free LREG for "
			   "another loop-wide live range: ");
	      print_gimple_stmt (dump_file, c.load, 0);
	    }
	  return false;
	}
      /* Fold reserve (-mtt-tensix-optimize-priced-placement): when this park
	 would take the last free LREG, weigh its benefit against the
	 downstream identity-reload fold, which needs a free register.  If the
	 fold outbids the park, leave the register free.  */
      if (lreg_budget == 1 && riscv_tt_opt_priced_placement > 0)
	{
	  rvtt_place_weight w = rvtt_place_loop_weight (c.loop);
	  int64_t park_bid
	    = w.proven ? rvtt_place_net_benefit (1, 1, w) : 0;
	  bool outbid
	    = rvtt_place_fold_reserve_outbids (fn, park_bid, w.proven,
					       "park-tier", dump_file);
	  if (outbid)
	    {
	      rvtt_refuse (RVTT_REF_PLACE_FOLD_RESERVE_OUTBID, dump_file,
			   "park-tier: refused (place-fold-reserve-outbid): "
			   "the priced fold demand outbids the marginal "
			   "LREG park: ");
	      if (dump_file)
		print_gimple_stmt (dump_file, c.load, 0);
	      return false;
	    }
	}
      peel_record *rec = ensure_peeled (c);
      /* Pre-peel placement (-mtt-tensix-optimize-park-ordering): the post-peel
	 placement leaves the peel's own copy of the materialization, so the
	 constant would be materialized twice per loop entry.  When all lanes
	 are proven enabled at the peel block (prepeel_ambient_all_lanes_p),
	 put the definition at its head and erase the copy; otherwise keep the
	 post-peel placement.  */
      bool prepeel = false;
      if (rec && riscv_tt_opt_park_ordering > 0)
	{
	  if (prepeel_ambient_all_lanes_p (rec->copy_bb))
	    prepeel = true;
	  else if (dump_file)
	    {
	      rvtt_refuse (RVTT_REF_PARK_PREPEEL_AMBIENT_UNPROVEN, dump_file,
			   "pressure-park: pre-peel placement refused "
			   "(park-prepeel-ambient-unproven, peel bb %d); "
			   "keeping the programming-point placement: ",
			   rec->copy_bb->index);
	      print_gimple_stmt (dump_file, c.load, 0);
	    }
	}
      basic_block preheader = prepeel ? rec->copy_bb
	: rvtt_commit_hoist_preheader (c.entry);
      gcall *chain_root = rvtt_chained_loadi_root (c.load);
      /* Same virtual-operand discipline as the invariant pass's hoist:
	 only a renamed SSA vdef has uses to unlink or a name to
	 release; the pass-level TODO renumbers the rest.  */
      if (tree vdef = gimple_vdef (c.load))
	{
	  if (TREE_CODE (vdef) == SSA_NAME)
	    {
	      unlink_stmt_vdef (c.load);
	      release_ssa_name (vdef);
	    }
	  gimple_set_vdef (c.load, NULL_TREE);
	}
      if (gimple_vuse (c.load))
	{
	  gimple_set_vuse (c.load, NULL_TREE);
	  update_stmt (c.load);
	}
      if (chain_root)
	{
	  if (tree vdef = gimple_vdef (chain_root))
	    {
	      if (TREE_CODE (vdef) == SSA_NAME)
		{
		  unlink_stmt_vdef (chain_root);
		  release_ssa_name (vdef);
		}
	      gimple_set_vdef (chain_root, NULL_TREE);
	    }
	  if (gimple_vuse (chain_root))
	    {
	      gimple_set_vuse (chain_root, NULL_TREE);
	      update_stmt (chain_root);
	    }
	}
      gimple_stmt_iterator from = gsi_for_stmt (c.load);
      bool erased_copy = false;
      if (prepeel)
	{
	  /* Head of the peel block: the parked definition executes
	     before the peeled iteration under the proven all-lanes
	     ambient.  */
	  gimple_stmt_iterator at = gsi_after_labels (preheader);
	  gimple *root = nullptr;
	  if (chain_root)
	    {
	      gimple_stmt_iterator root_from = gsi_for_stmt (chain_root);
	      root = gsi_stmt (root_from);
	      gsi_remove (&root_from, false);
	    }
	  gimple *load = gsi_stmt (from);
	  gsi_remove (&from, false);
	  if (gsi_end_p (at))
	    {
	      if (root)
		{
		  gsi_insert_before (&at, root, GSI_NEW_STMT);
		  gsi_insert_after (&at, load, GSI_NEW_STMT);
		}
	      else
		gsi_insert_before (&at, load, GSI_NEW_STMT);
	    }
	  else
	    {
	      if (root)
		gsi_insert_before (&at, root, GSI_SAME_STMT);
	      gsi_insert_before (&at, load, GSI_SAME_STMT);
	    }
	  /* Erase the peel's copy of the materialization: the moved definition
	     dominates the peel block and writes the same constant in every
	     lane.  */
	  tree lhs = gimple_call_lhs (c.load);
	  if (tree *fresh = rec->names.get (lhs))
	    if (*fresh && TREE_CODE (*fresh) == SSA_NAME)
	      {
		gimple *cp = SSA_NAME_DEF_STMT (*fresh);
		if (cp && gimple_bb (cp) == rec->copy_bb)
		  {
		    replace_uses_by (*fresh, lhs);
		    gimple_stmt_iterator cgsi = gsi_for_stmt (cp);
		    gsi_remove (&cgsi, true);
		    release_defs (cp);
		    /* The copied root of a lowered pair dies with the copied
		       tail.  */
		    if (chain_root)
		      if (tree *fresh_root
			  = rec->names.get (gimple_call_lhs (chain_root)))
			if (*fresh_root && TREE_CODE (*fresh_root) == SSA_NAME)
			  {
			    gimple *rcp = SSA_NAME_DEF_STMT (*fresh_root);
			    if (rcp && gimple_bb (rcp) == rec->copy_bb)
			      {
				gimple_stmt_iterator rgsi = gsi_for_stmt (rcp);
				gsi_remove (&rgsi, true);
				release_defs (rcp);
			      }
			  }
		    erased_copy = true;
		  }
	      }
	}
      else
	{
	  if (chain_root)
	    {
	      gimple_stmt_iterator root_from = gsi_for_stmt (chain_root);
	      gsi_move_to_bb_end (&root_from, preheader);
	    }
	  gsi_move_to_bb_end (&from, preheader);
	}
      --lreg_budget;
      if (dump_file)
	{
	  if (prepeel)
	    fprintf (dump_file,
		     "pressure-park: hoisted invariant materialization to "
		     "a free LREG at the pre-peel entry (peel bb %d; "
		     "ambient all-lanes proven; peel duplicate %s): ",
		     preheader->index,
		     erased_copy ? "erased" : "not-found");
	  else
	    fprintf (dump_file,
		     "pressure-park: hoisted invariant materialization to "
		     "a free LREG at the programming point (preheader "
		     "bb %d): ",
		     preheader->index);
	  print_gimple_stmt (dump_file, c.load, 0);
	}
      return true;
    };

  /* An own-entry loop has no peeled copy whose inline materializations
     bound the function's peak, and the CC machinery adds LREG
     temporaries only at RTL, which the model cannot see.  Price its
     parks against the function before this walk rewrites the in-loop
     materializations, which keeps the margin the peeled copy gave.  */
  for (residency_candidate &c : loop_cands)
    if (c.own_entry)
      {
	lreg_budget = rvtt_pressure_residual (fn);
	break;
      }

  auto_vec<residency_candidate *> lreg_only_cands;
  auto lreg_only_p = [] (residency_candidate &c) -> bool
    {
      tree name = gimple_call_lhs (c.load);
      if (rvtt_pressure_lut_slot_use_p (name))
	return true;
      imm_use_iterator uit;
      gimple *use;
      FOR_EACH_IMM_USE_STMT (use, uit, name)
	if (compare_vd_use_p (use, name))
	  return true;
      return false;
    };
  for (residency_candidate &c : loop_cands)
    {
      /* Store-source tier (-mtt-tensix-optimize-store-source-tier): a
	 store-consumed LOOP constant tries the LREG tier first, because an
	 LREG-resident value can be stored directly and needs no per-row SFPMOV
	 copy out of the constant registers.  If the tier refuses, the
	 candidate falls through to PRGM placement, whose one-word copy per row
	 still beats rematerializing the constant in the loop.  The store-sink
	 case is handled inside place().  */
      if (riscv_tt_opt_store_source_tier > 0
	  && riscv_tt_opt_store_sink <= 0
	  && store_consumed_p (c))
	{
	  if (dump_file)
	    {
	      fprintf (dump_file, "const-residency: store-source-tier "
		       "(store-source-encoding-ceiling): ");
	      print_gimple_stmt (dump_file, c.load, 0);
	      /* Dump the bids: the LREG park saves the materialization and the
		 copy; the PRGM park keeps the copy and adds the
		 programming.  */
	      rvtt_place_weight w = rvtt_place_loop_weight (c.loop);
	      if (w.proven)
		fprintf (dump_file,
			 "placement-arbiter: store-source bb %d lreg-park "
			 "bid %" PRId64 " vs prgm-park bid %" PRId64
			 " -> lreg-tier-first\n",
			 gimple_bb (c.load)->index,
			 rvtt_place_net_benefit (2, 1, w),
			 rvtt_place_net_benefit (0, 2, w));
	      else
		fprintf (dump_file,
			 "placement-arbiter: store-source bb %d bids "
			 "unpriceable (trip weight unproven); the "
			 "established tier order stands\n",
			 gimple_bb (c.load)->index);
	    }
	  if (lreg_hoist (c))
	    {
	      changed = true;
	      continue;
	    }
	}
      /* A LUT table slot reads a fixed LREG, and a compare's VD cannot
	 be a PRGM register, so a PRGM park would be copied back into an
	 LREG on every row.  Such candidates take the LREG tier after the
	 others have placed, so that the PRGM programming (staged through
	 L0) precedes the parked values and every PRGM taker has already
	 left the LREG file.  */
      if (riscv_tt_opt_pressure_park > 0 && lreg_only_p (c))
	{
	  lreg_only_cands.safe_push (&c);
	  continue;
	}
      changed |= place (c) || lreg_hoist (c);
    }
  /* The flat one-register charge per park reserves room for the LREG
     temporaries CC lowering adds at RTL.  A loop without SFPU barriers
     has none, so measure the residual there instead: a parked LUT slot
     value is already live at its LUT and often costs nothing.  */
  for (residency_candidate *c : lreg_only_cands)
    {
      if (!rvtt_loop_has_sfpu_barrier_p (c->loop))
	lreg_budget = rvtt_pressure_residual (fn);
      if (lreg_hoist (*c))
	changed = true;
      else if (rvtt_pressure_lut_slot_use_p (gimple_call_lhs (c->load)))
	changed |= place (*c);
    }

  /* MAD-PAIR groups are placed all or none: a half-placed pair pays for
     programming while the surviving immediate fold still blocks the mad
     combine.  Simulate place()'s register choice (existing allocation, free
     register, TU same-value reuse) for the whole group first.  */
  for (unsigned gx = 0; gx < madpair_cands.length (); )
    {
      unsigned group = madpair_cands[gx].group;
      unsigned gend = gx;
      while (gend < madpair_cands.length ()
	     && madpair_cands[gend].group == group)
	++gend;
      bool ok = !invalid_madpair_groups.contains (group);
      if (ok)
	{
	  unsigned sim = st->claimed;
	  auto_vec<unsigned, 3> sim_vals;
	  for (unsigned ix = gx; ok && ix != gend; ++ix)
	    {
	      unsigned value = madpair_cands[ix].value;
	      bool have = false;
	      for (prgm_alloc &a : st->allocs)
		if (a.value == value)
		  {
		    have = true;
		    break;
		  }
	      for (unsigned v : sim_vals)
		have |= v == value;
	      if (have)
		continue;
	      unsigned reg = 0;
	      for (unsigned r : prgm_regs)
		if (!(sim & (1u << r)))
		  {
		    reg = r;
		    break;
		  }
	      if (!reg)
		{
		  const prgm_tu_facts &tu = tu_prgm_facts ();
		  for (unsigned r : prgm_regs)
		    if ((tu.value_known & (1u << r)) && tu.value[r] == value)
		      {
			reg = r;
			break;
		      }
		}
	      if (!reg)
		ok = false;
	      else
		{
		  sim |= 1u << reg;
		  sim_vals.safe_push (value);
		}
	    }
	  if (!ok && dump_file)
	    rvtt_refuse (RVTT_REF_MADPAIR_PRGM_EXHAUSTED, dump_file,
			 "const-residency: madpair group refused "
			 "(madpair-prgm-exhausted): the pair needs more PRGM "
			 "registers than remain free\n");
	}
      if (ok)
	for (unsigned ix = gx; ix != gend; ++ix)
	  changed |= place (madpair_cands[ix]);
      gx = gend;
    }

  /* HOISTED-REUSE: place() as usual; a refused candidate keeps its hoisted
     LREG placement.  */
  for (residency_candidate &c : hoistreuse_cands)
    changed |= place (c);

  for (residency_candidate &c : pressure_cands)
    changed |= place (c);

  /* REPEAT groups: a free register, then TU same-value reuse, then a
     dead-claim reclaim.  The reclaim is safe because the function has no call
     or asm between the entry programming and the reads.  */
  for (repeat_group *g : repeat_groups)
    {
      bool known = false;
      for (prgm_alloc &a : st->allocs)
	known |= a.value == g->value;
      if (known)
	continue;
      const prgm_tu_facts &tu = tu_prgm_facts ();
      unsigned reg = 0;
      bool reclaimed = false;
      for (unsigned r : prgm_regs)
	if (!(st->claimed & (1u << r)))
	  {
	    reg = r;
	    break;
	  }
      if (!reg)
	for (unsigned r : prgm_regs)
	  if ((tu.value_known & (1u << r)) && tu.value[r] == g->value)
	    {
	      bool repurposed = false;
	      for (prgm_alloc &a : st->allocs)
		repurposed |= a.reg == r;
	      if (!repurposed)
		{
		  reg = r;
		  break;
		}
	    }
      if (!reg)
	for (unsigned r : prgm_regs)
	  {
	    bool in_allocs = false;
	    for (prgm_alloc &a : st->allocs)
	      in_allocs |= a.reg == r;
	    if (!in_allocs && (tu.claimed & (1u << r))
		&& !(tu.creg_read & (1u << r)))
	      {
		reg = r;
		reclaimed = true;
		break;
	      }
	  }
      if (!reg)
	{
	  if (dump_file)
	    fprintf (dump_file, "const-residency: repeat group 0x%08x "
		     "refused (prgm-exhausted)\n", g->value);
	  continue;
	}
      st->claimed |= 1u << reg;

      edge entry = single_succ_edge (ENTRY_BLOCK_PTR_FOR_FN (fn));
      basic_block prog_bb = entry->dest;
      if (!single_pred_p (prog_bb))
	prog_bb = split_edge (entry);
      auto_vec<gcall *> seq;
      tree staged = rvtt_build_loadimm32 (g->value, &seq);
      gcall *wrcfg = gimple_build_call
	(wrcfg_d->decl, 3, staged, build_int_cst (unsigned_type_node, 0),
	 build_int_cst (unsigned_type_node, reg));
      gimple_stmt_iterator at = gsi_after_labels (prog_bb);
      for (gcall *ld : seq)
	gsi_insert_before (&at, ld, GSI_SAME_STMT);
      gsi_insert_before (&at, wrcfg, GSI_SAME_STMT);
      st->allocs.safe_push (prgm_alloc { g->value, reg, prog_bb,
					 reclaimed, false, true });
      for (gcall *m : g->members)
	replace_with_prgm_read (m, reg);
      changed = true;
      if (dump_file)
	fprintf (dump_file,
		 "const-residency: repeat group 0x%08x: %u materializations "
		 "read PRGM L%u%s, programmed once at the function entry "
		 "(bb %d; %d issue words saved per call)\n",
		 g->value, g->members.length (), reg,
		 reclaimed ? " (DEAD claim reclaimed)" : "",
		 prog_bb->index, g->saved);
    }

  for (peel_record *rec : peel_records)
    delete rec;
  delete arb_model;
  return changed;
}
