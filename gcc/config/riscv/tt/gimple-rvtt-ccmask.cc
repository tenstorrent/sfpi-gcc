/* Fold single-zero-assign SFPU CC regions into value-mask arithmetic.
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

/* -mtt-tensix-optimize-ccmask (default off).

   Fold a predicated zeroing conditional such as

       v_if (x <= 0.0f) { z = 0.0f; } v_endif

   which reaches this pass as the structured CC skeleton

       sfppushc (0)
       tok = sfpxpred (IF|PUSH|depth 1, 0)
       c   = sfpxcmp (x, 0.0f, TYPE_FLOAT|CC)
       cb  = sfpxcond (mod, tok, c)
       zv  = <zero materialization>
       z'  = sfpassign_lv (z, zv)
       sfpxpred (depth 1, cb)
       sfppopc (0)

   and expands to a serial chain of five CC words (SETCC pair, COMPC,
   predicated move, ENCC), into two independent vector words:

       mask = SFPGT/SFPLE (x, 0, SET_DEST)    -- keep-mask
       z'   = SFPAND (z, mask)

   This relies on the compare being a float test against +0.0.  The CC
   lowering of "x <= 0.0f" zeroes the lanes with the sign set or an
   all-zero encoding; SFPGT compares in sign-magnitude total order, so
   SFPGT (x, 0) is all-ones exactly on the other lanes, including both
   zeros, NaNs and infinities.  "x > 0.0f" uses SFPLE.  "x < 0.0f" and
   "x >= 0.0f" test only the sign bit and use the swapped compares
   SFPLE (0, x) and SFPGT (0, x).  SET_DEST writes the first operand, so
   the swapped forms reuse the region's own zero materialization as that
   operand.  With -mtt-tensix-optimize-cc-region-general, EQ and NE are
   also expressible as two-compare compositions, but these are refused
   as unprofitable (eqne_fold_priced_profitable_p).  Each form, and each
   EQ/NE composition, was checked against the CC lowering exhaustively
   over all 2^32 inputs.

   The pass also folds the Blackhole sign-magnitude to two's-complement
   conversion v_if (x < 0) { x = setsgn (x, 0); x = 0 - x; } v_endif
   into SFPABS, SFPXOR and SFPSETSGN (see match_smag_group).

   The replacement instructions execute under the enclosing CC state, so
   a fold inside an enclosing v_if keeps its semantics, and the deleted
   pushc/popc pair is stack-neutral.  The pass runs before
   pass_rvtt_invariant because the removed CC statement would otherwise
   prevent hoisting immediates out of the containing loop.  SFPGT and
   SFPLE only exist on Blackhole; other targets are left alone.  */

#define INCLUDE_ALGORITHM
#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "gimple.h"
#include "tree-pass.h"
#include "ssa.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "tree-cfg.h"
#include "dominance.h"
#include "tree-ssa-loop-niter.h"
#include "cfgloop.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-refuse.h"
#include "rvtt-cc-region.h"
#include "rvtt-delivery-cost.h"

namespace {

static unsigned n_folded;

/* For the sfpxcmp CMP against a constant operand, return the
   non-constant operand in *VALUE and the constant in *CST.  Return false
   for a vector-vs-vector compare.  */

static bool
rvtt_cmp_value_and_cst (gcall *cmp, tree *value, uint32_t *cst)
{
  rvtt_arg_info a0 (gimple_call_arg (cmp, 0));
  rvtt_arg_info a1 (gimple_call_arg (cmp, 1));
  if (a1.is_cst () && !a0.is_cst ())
    {
      *value = a0.get_arg ();
      *cst = a1.get_cst ();
      return true;
    }
  if (a0.is_cst () && !a1.is_cst ())
    {
      *value = a1.get_arg ();
      *cst = a0.get_cst ();
      return true;
    }
  return false;
}

/* Record the named refusal REASON at STMT and return false.  */

static bool
refuse (const char *reason, gimple *stmt)
{
  rvtt_refuse_by_name_at (reason, stmt, dump_file,
			  "ccmask refused (%s): ", reason);
  if (dump_file)
    print_gimple_stmt (dump_file, stmt, 0);
  return false;
}

/* Return the defining call if VAL is an immediate load of 0 into an
   allocatable LREG, which a SET_DEST compare can overwrite (the
   read-only constant register does not qualify).  */

static gcall *
writable_zero_def (tree val)
{
  if (TREE_CODE (val) != SSA_NAME)
    return nullptr;
  gimple *def = SSA_NAME_DEF_STMT (val);
  const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
  if (!insnd)
    return nullptr;
  gcall *call = as_a <gcall *> (def);
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpxloadi:
    case rvtt_insn_data::sfploadi:
      return rvtt_call_int_arg (call, 1) == 0 ? call : nullptr;
    default:
      return nullptr;
    }
}

struct ccmask_group
{
  gcall *pushc, *xvif, *fcmp, *condb, *assign, *end, *popc;
  tree x;	  /* compared vector */
  tree z;	  /* carried live value */
  tree zv;	  /* the assigned zero value */
  unsigned cc;	  /* SFPXCMP_MOD1_CC_* of the source compare */
};

static bool check_compare_form (ccmask_group *g);

/* Match the structured skeleton starting at the sfppushc at GSI and
   fill G.  Set *CANDIDATE once the region is known to be a zeroing
   conditional, after which failures are reported by name.  Only the
   linear and diamond-closed layouts are accepted, and the matched frame
   must agree with the CC-region tree CCR; a disagreement is a compiler
   bug, asserted with flag_checking.  */

static bool
match_group (const rvtt_cc_region_tree *ccr, gimple_stmt_iterator gsi,
	     ccmask_group *g, bool *candidate)
{
  enum { WANT_XVIF, WANT_FCMP, WANT_CONDB, WANT_ASSIGN, WANT_POPC, DONE }
    want = WANT_XVIF;

  *candidate = false;
  memset (g, 0, sizeof (*g));

  gcall *pushc = rvtt_call_with_id (gsi_stmt (gsi), rvtt_insn_data::sfppushc);
  if (!pushc || rvtt_call_int_arg (pushc, 0) != 0)
    return false;
  g->pushc = pushc;

  gsi_next (&gsi);
  for (unsigned steps = 0; !gsi_end_p (gsi) && steps < 64; gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL)
	continue;
      steps++;

      /* Scalar plumbing (e.g. instruction-buffer address loads) is not
	 part of the vector region; it stays where it is.  */
      if (gimple_code (stmt) == GIMPLE_ASSIGN)
	{
	  tree lhs = gimple_get_lhs (stmt);
	  if (lhs && TREE_CODE (lhs) == SSA_NAME
	      && !VECTOR_TYPE_P (TREE_TYPE (lhs)))
	    continue;
	  return *candidate ? refuse ("ccmask-region-foreign-stmt", stmt)
			    : false;
	}

      const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
      if (!insnd)
	return *candidate ? refuse ("ccmask-region-foreign-stmt", stmt)
			  : false;
      gcall *call = as_a <gcall *> (stmt);

      switch (insnd->id)
	{
	case rvtt_insn_data::sfpxpred:
	  if (want == WANT_XVIF)
	    {
	      if (rvtt_call_int_arg (call, 0)
		  != (SFPXPRED_MOD1_IF | SFPXPRED_MOD1_PUSH
		      | (1u << SFPXPRED_MOD1_DEPTH_SHIFT))
		  || !integer_zerop (gimple_call_arg (call, 1)))
		return false;
	      g->xvif = call;
	      want = WANT_FCMP;
	      continue;
	    }
	  if (want == WANT_POPC && !g->end
	      && rvtt_call_int_arg (call, 0)
		 == (1u << SFPXPRED_MOD1_DEPTH_SHIFT)
	      && gimple_call_arg (call, 1) == gimple_call_lhs (g->condb))
	    {
	      g->end = call;
	      continue;
	    }
	  return *candidate ? refuse ("ccmask-region-shape", stmt) : false;

	case rvtt_insn_data::sfpxcmp:
	  {
	    if (want != WANT_FCMP)
	      return *candidate ? refuse ("ccmask-region-shape", stmt)
				: false;
	    /* Only a float compare against +0.0 is handled; the kind is
	       checked in check_compare_form.  */
	    tree cmp_value;
	    uint32_t cmp_cst;
	    if (!rvtt_cmp_value_and_cst (call, &cmp_value, &cmp_cst))
	      return *candidate
		? refuse ("ccmask-compare-kind-unsupported", stmt) : false;
	    g->fcmp = call;
	    g->x = cmp_value;
	    want = WANT_CONDB;
	    continue;
	  }

	case rvtt_insn_data::sfpxcond:
	  {
	    if (want != WANT_CONDB)
	      return *candidate ? refuse ("ccmask-region-shape", stmt)
				: false;
	    /* sfpxcond is (mod, pred, cond).  */
	    tree c = gimple_call_arg (call, 2);
	    tree t = gimple_call_arg (call, 1);
	    if (TREE_CODE (c) != SSA_NAME || TREE_CODE (t) != SSA_NAME
		|| SSA_NAME_DEF_STMT (c) != g->fcmp
		|| SSA_NAME_DEF_STMT (t) != g->xvif
		|| !has_single_use (c) || !has_single_use (t))
	      return *candidate ? refuse ("ccmask-region-shape", stmt)
				: false;
	    g->condb = call;
	    want = WANT_ASSIGN;
	    continue;
	  }

	case rvtt_insn_data::sfpassign_lv:
	  {
	    if (want != WANT_ASSIGN)
	      return *candidate ? refuse ("ccmask-region-shape", stmt)
				: false;
	    /* The predicated statement assigns zero; from here on
	       refusals are reported by name.  */
	    if (!rvtt_zero_vector_p (gimple_call_arg (call, 1)))
	      return false;
	    *candidate = true;
	    g->assign = call;
	    g->z = gimple_call_arg (call, 0);
	    g->zv = gimple_call_arg (call, 1);
	    want = WANT_POPC;
	    continue;
	  }

	case rvtt_insn_data::sfppopc:
	  {
	    if (want != WANT_POPC || !g->end
		|| rvtt_call_int_arg (call, 0) != 0)
	      return *candidate ? refuse ("ccmask-region-shape", stmt)
				: false;
	    g->popc = call;
	    goto region_closed;
	  }

	case rvtt_insn_data::sfppushc:
	case rvtt_insn_data::sfpcompc:
	  /* A nested region or an else-arm is not a single zeroing
	     assignment.  */
	  return *candidate ? refuse ("ccmask-region-shape", stmt) : false;

	case rvtt_insn_data::sfpxloadi:
	case rvtt_insn_data::sfploadi:
	case rvtt_insn_data::sfploadi_lv:
	case rvtt_insn_data::sfpreadlreg:
	  /* Pure LREG materializations, such as the zero itself.  */
	  continue;

	default:
	  /* Anything else with target side effects or CC involvement is
	     not this shape.  */
	  if (insnd->sets_cc (call) || insnd->has_side_effects (call))
	    return *candidate ? refuse ("ccmask-region-foreign-stmt", stmt)
			      : false;
	  continue;
	}
    }
  /* The block ended before the closing sfppopc.  v_endif can place the
     popc in a diamond: the body block jumps to a join with exactly two
     predecessors, the body block and a block whose only statement is
     sfppopc (0) and whose single successor is the join.  */
  if (want == WANT_POPC)
    {
      basic_block body_bb = gimple_bb (g->assign);
      if (single_succ_p (body_bb))
	{
	  basic_block join = single_succ (body_bb);
	  if (EDGE_COUNT (join->preds) == 2)
	    {
	      basic_block popc_bb = EDGE_PRED (join, 0)->src == body_bb
		? EDGE_PRED (join, 1)->src : EDGE_PRED (join, 0)->src;
	      gcall *popc = nullptr;
	      bool only = true;
	      for (gimple_stmt_iterator psi = gsi_start_bb (popc_bb);
		   !gsi_end_p (psi) && only; gsi_next (&psi))
		{
		  gimple *pstmt = gsi_stmt (psi);
		  if (is_gimple_debug (pstmt)
		      || gimple_code (pstmt) == GIMPLE_LABEL)
		    continue;
		  if (gcall *pc = rvtt_call_with_id (pstmt,
						rvtt_insn_data::sfppopc))
		    {
		      if (popc || rvtt_call_int_arg (pc, 0) != 0)
			only = false;
		      else
			popc = pc;
		    }
		  else
		    only = false;
		}
	      if (only && popc && single_succ_p (popc_bb)
		  && single_succ (popc_bb) == join)
		{
		  g->popc = popc;
		  goto region_closed;
		}
	    }
	}
    }
  return *candidate ? refuse ("ccmask-region-open-cfg", g->pushc) : false;

 region_closed:
  if (!g->end)
    return *candidate ? refuse ("ccmask-region-shape", g->popc) : false;
  /* The matched frame must be the one the CC-region tree computed.  */
  {
    rvtt_cc_region *r = ccr->region_opened_by (g->pushc);
    bool tree_ok = r && ccr->refinements_pure_p (r)
      && ccr->region_of (g->assign) == r
      && r->exits.length () == 1 && r->exits[0] == g->popc
      && r->refinements.length () == 4
      && r->refinements[0] == g->xvif
      && r->refinements[1] == g->fcmp
      && r->refinements[2] == g->condb
      && r->refinements[3] == g->end;
    if (flag_checking)
      gcc_assert (tree_ok);
    if (!tree_ok)
      return refuse ("ccmask-region-shape", g->popc);
  }

  return check_compare_form (g);
}

/* Return true if the EQ/NE two-compare composition is cheaper than the
   CC skeleton it replaces, per the delivery-cost model.

   The composition inserts six words, as emitted by transform_group: a
   copy of X (the direct compare's SET_DEST overwrites it while the
   swapped compare still reads it), the two compares, the mask combine,
   the writable zero for the swapped compare, and the final AND.  It
   removes two: the single SETCC of the EQ/NE compare and the predicated
   move.  The pushc/popc/encc words are not counted, since whether they
   are emitted depends on later CC canonicalization; omitting them only
   biases toward refusal.  The single-compare folds (two words for five)
   are always profitable and are not checked here.  */

static bool
eqne_fold_priced_profitable_p (void)
{
  const int64_t inserted_words = 6;
  const int64_t removed_words = 2;
  int64_t inserted
    = rvtt_dcost_words_to_centislots (inserted_words,
				      rvtt_delivery_cost::PLANE_RISC_PUSH);
  int64_t removed
    = rvtt_dcost_words_to_centislots (removed_words,
				      rvtt_delivery_cost::PLANE_RISC_PUSH);
  return inserted < removed;
}

/* Check the compare of G, for both match_group and match_group_general:
   it must be a float test against +0.0 whose complement is a single GT
   or LE keep-mask, or, with -mtt-tensix-optimize-cc-region-general, an
   EQ/NE test that also passes eqne_fold_priced_profitable_p.  Set
   G->cc.  */

static bool
check_compare_form (ccmask_group *g)
{
  {
    const rvtt_insn_data *cmp_insnd = rvtt_get_insn_data (g->fcmp);
    long mod = rvtt_call_int_arg (g->fcmp, cmp_insnd->mod_arg ());
    if (mod < 0)
      return refuse ("ccmask-compare-form", g->fcmp);
    unsigned type = ((unsigned) mod >> SFPXCMP_MOD1_TYPE_SHIFT)
      & SFPXCMP_MOD1_TYPE_MASK;
    if (type != SFPXCMP_MOD1_TYPE_FLOAT)
      return refuse ("ccmask-compare-kind-unsupported", g->fcmp);
    g->cc = (unsigned) mod & SFPXCMP_MOD1_CC_MASK;
    bool eqne = (g->cc == SFPXCMP_MOD1_CC_EQ || g->cc == SFPXCMP_MOD1_CC_NE)
      && riscv_tt_opt_cc_region_general > 0;
    if (g->cc != SFPXCMP_MOD1_CC_LE && g->cc != SFPXCMP_MOD1_CC_GT
	&& g->cc != SFPXCMP_MOD1_CC_LT && g->cc != SFPXCMP_MOD1_CC_GE
	&& !eqne)
      /* EQ/NE have no single-compare complement.  */
      return refuse ("ccmask-compare-direction-unsupported", g->fcmp);
    if (g->cc == SFPXCMP_MOD1_CC_LT || g->cc == SFPXCMP_MOD1_CC_GE || eqne)
      {
	/* The swapped-operand compares (0 <= x, 0 > x), also used by
	   EQ/NE, overwrite their first operand, so reuse the region's
	   zero, which must be writable and have no other uses.  */
	gcall *zdef = writable_zero_def (g->zv);
	if (!zdef)
	  return refuse ("ccmask-zero-not-writable", g->assign);
	if (!has_single_use (g->zv))
	  return refuse ("ccmask-zero-shared", g->assign);
      }
    /* Only a compare against zero lowers to a pure sign/zero test.  */
    {
      tree cmp_value;
      uint32_t cmp_cst;
      if (!rvtt_cmp_value_and_cst (g->fcmp, &cmp_value, &cmp_cst)
	  || cmp_cst != 0)
	return refuse ("ccmask-boundary-unsupported", g->fcmp);
    }
  }

  if (TREE_CODE (g->x) != SSA_NAME || TREE_CODE (g->z) != SSA_NAME)
    return refuse ("ccmask-operand-form", g->assign);

  /* Checked last, so the cost refusal only names otherwise valid
     EQ/NE shapes.  */
  if ((g->cc == SFPXCMP_MOD1_CC_EQ || g->cc == SFPXCMP_MOD1_CC_NE)
      && !eqne_fold_priced_profitable_p ())
    return refuse ("ccmask-eqne-fold-unprofitable", g->fcmp);

  return true;
}

/* Return true if A executes before B on every execution of B: A precedes
   B in the same block, or A's block dominates B's.  */

static bool
stmt_ordered_before_p (gimple *a, gimple *b)
{
  basic_block ba = gimple_bb (a);
  basic_block bb = gimple_bb (b);
  if (!ba || !bb || a == b)
    return false;
  if (ba == bb)
    {
      for (gimple_stmt_iterator gsi = gsi_for_stmt (a); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	if (gsi_stmt (gsi) == b)
	  return true;
      return false;
    }
  return dominated_by_p (CDI_DOMINATORS, bb, ba);
}

/* With -mtt-tensix-optimize-cc-region-general, match the zeroing
   conditional opened by PUSHC in FUN for any block layout, using the
   CC-region tree CCR, and fill G; *CANDIDATE is as for match_group.
   The conditions are:
   - PUSHC opens a structured, refinement-pure, unpoisoned frame R with
     no nested frames and exactly one exit popc;
   - R's refinement chain is exactly [xvif, fcmp, condb, end], linked as
     in match_group;
   - R's other members are scalar statements, scalar control flow, pure
     LREG materializations, side-effect-free CC-inert calls, and exactly
     one predicated statement, the zeroing sfpassign_lv;
   - each of pushc, xvif, fcmp, condb, assign, end, popc executes before
     the next, so the assign executes exactly when the region does.  */

static bool
match_group_general (function *fun, const rvtt_cc_region_tree *ccr,
		     gcall *pushc, ccmask_group *g, bool *candidate)
{
  *candidate = false;
  memset (g, 0, sizeof (*g));

  rvtt_cc_region *r = ccr->region_opened_by (pushc);
  if (!r)
    return false;
  const vec<gimple *> &chain = ccr->refinement_chain (r);
  if (chain.length () != 4
      || !rvtt_call_with_id (chain[0], rvtt_insn_data::sfpxpred)
      || !rvtt_call_with_id (chain[1], rvtt_insn_data::sfpxcmp)
      || !rvtt_call_with_id (chain[2], rvtt_insn_data::sfpxcond)
      || !rvtt_call_with_id (chain[3], rvtt_insn_data::sfpxpred))
    return false;

  g->pushc = pushc;
  g->xvif = as_a <gcall *> (chain[0]);
  g->fcmp = as_a <gcall *> (chain[1]);
  g->condb = as_a <gcall *> (chain[2]);
  g->end = as_a <gcall *> (chain[3]);
  if (rvtt_call_int_arg (g->xvif, 0)
	!= (SFPXPRED_MOD1_IF | SFPXPRED_MOD1_PUSH
	    | (1u << SFPXPRED_MOD1_DEPTH_SHIFT))
      || !integer_zerop (gimple_call_arg (g->xvif, 1))
      || rvtt_call_int_arg (g->end, 0)
	 != (1u << SFPXPRED_MOD1_DEPTH_SHIFT)
      || gimple_call_arg (g->end, 1) != gimple_call_lhs (g->condb))
    return false;
  {
    /* The compared value is the non-constant operand of sfpxcmp.  */
    tree cmp_value;
    uint32_t cmp_cst;
    if (!rvtt_cmp_value_and_cst (g->fcmp, &cmp_value, &cmp_cst))
      return false;
    g->x = cmp_value;
  }

  /* Member scan over the tree's statement mapping.  */
  gcall *assign = nullptr;
  unsigned n_assigns = 0;
  bool nested = false;
  gimple *foreign = nullptr;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	rvtt_cc_region *rs = ccr->region_of (stmt);
	if (!rs || rs == r->parent)
	  continue;
	bool member = rs == r;
	if (!member)
	  {
	    for (rvtt_cc_region *a = rs->parent; a; a = a->parent)
	      if (a == r)
		{
		  nested = true;
		  break;
		}
	    continue;
	  }
	if (stmt == g->pushc || stmt == g->xvif || stmt == g->fcmp
	    || stmt == g->condb || stmt == g->end)
	  continue;
	if (ccr->closes_frame_p (stmt, r))
	  continue;
	if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
	    || gimple_code (stmt) == GIMPLE_COND
	    || gimple_code (stmt) == GIMPLE_GOTO)
	  continue;
	if (gimple_code (stmt) == GIMPLE_ASSIGN)
	  {
	    tree lhs = gimple_get_lhs (stmt);
	    if (lhs && TREE_CODE (lhs) == SSA_NAME
		&& !VECTOR_TYPE_P (TREE_TYPE (lhs)))
	      continue;		/* scalar plumbing */
	    foreign = stmt;
	    continue;
	  }
	const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
	if (!insnd)
	  {
	    foreign = stmt;
	    continue;
	  }
	gcall *call = as_a <gcall *> (stmt);
	switch (insnd->id)
	  {
	  case rvtt_insn_data::sfpassign_lv:
	    n_assigns++;
	    if (!assign && rvtt_zero_vector_p (gimple_call_arg (call, 1)))
	      assign = call;
	    break;
	  case rvtt_insn_data::sfpxloadi:
	  case rvtt_insn_data::sfploadi:
	  case rvtt_insn_data::sfploadi_lv:
	  case rvtt_insn_data::sfpreadlreg:
	    break;		/* pure LREG value materializations */
	  default:
	    if (insnd->sets_cc (call) || insnd->has_side_effects (call))
	      foreign = stmt;
	    break;
	  }
      }

  if (!assign)
    /* Not a zeroing conditional; no refusal.  */
    return false;
  *candidate = true;
  g->assign = assign;
  g->z = gimple_call_arg (assign, 0);
  g->zv = gimple_call_arg (assign, 1);

  if (n_assigns != 1 || foreign)
    return refuse ("ccmask-region-foreign-stmt",
		   foreign ? foreign : (gimple *) g->assign);
  if (nested
      || !ccr->structured_p (r)
      || !ccr->refinements_pure_p (r)
      || ccr->poisoned_p (r))
    return refuse ("ccmask-region-shape", g->pushc);
  if (r->exits.length () != 1)
    return refuse ("ccmask-region-open-cfg", g->pushc);
  g->popc = as_a <gcall *> (r->exits[0]);

  /* Check the operand linkage of the condition as match_group does.  */
  {
    /* sfpxcond is (mod, pred, cond): the predicate is at mod_arg()+1
       and the condition at mod_arg()+2, as gimple-rvtt-pred.cc
       expand_vif builds it.  */
    const rvtt_insn_data *cond_insnd = rvtt_get_insn_data (g->condb);
    tree c = gimple_call_arg (g->condb, cond_insnd->mod_arg () + 2);
    tree t = gimple_call_arg (g->condb, cond_insnd->mod_arg () + 1);
    if (TREE_CODE (c) != SSA_NAME || TREE_CODE (t) != SSA_NAME
	|| SSA_NAME_DEF_STMT (c) != g->fcmp
	|| SSA_NAME_DEF_STMT (t) != g->xvif
	|| !has_single_use (c) || !has_single_use (t))
      return refuse ("ccmask-region-shape", g->condb);
  }

  /* Each statement of the structure executes before the next.  */
  if (!stmt_ordered_before_p (g->pushc, g->xvif)
      || !stmt_ordered_before_p (g->xvif, g->fcmp)
      || !stmt_ordered_before_p (g->fcmp, g->condb)
      || !stmt_ordered_before_p (g->condb, g->assign)
      || !stmt_ordered_before_p (g->assign, g->end)
      || !stmt_ordered_before_p (g->end, g->popc))
    return refuse ("ccmask-region-layout-unproven", g->pushc);

  if (!check_compare_form (g))
    return false;

  if (dump_file)
    fprintf (dump_file,
	     "ccmask: tree-keyed layout admission (cc-region-general) "
	     "for frame at bb %d\n", gimple_bb (g->pushc)->index);
  return true;
}

/* Replace the region matched in G with the keep-mask form.  */

static void
transform_group (ccmask_group *g)
{
  /* The keep-mask is the complement of the zeroing condition:
     x <= 0  ->  SFPGT (x, 0);    x > 0  ->  SFPLE (x, 0);
     x <  0  ->  SFPLE (0, x);    x >= 0 ->  SFPGT (0, x);
     x == 0  ->  SFPOR  (SFPGT (x, 0), SFPGT (0, x));
     x != 0  ->  SFPAND (SFPLE (x, 0), SFPLE (0, x)).
     A swapped compare overwrites the region's writable zero; a direct
     one reads the constant-zero register.  */
  bool eqne = (g->cc == SFPXCMP_MOD1_CC_EQ || g->cc == SFPXCMP_MOD1_CC_NE);
  bool swapped = (g->cc == SFPXCMP_MOD1_CC_LT
		  || g->cc == SFPXCMP_MOD1_CC_GE);
  const rvtt_insn_data *cmp_insnd
    = rvtt_get_insn_data ((g->cc == SFPXCMP_MOD1_CC_LE
			   || g->cc == SFPXCMP_MOD1_CC_GE
			   || g->cc == SFPXCMP_MOD1_CC_EQ)
			  ? rvtt_insn_data::sfpgt : rvtt_insn_data::sfple);
  const rvtt_insn_data *and_insnd
    = rvtt_get_insn_data (rvtt_insn_data::sfpand);
  const rvtt_insn_data *or_insnd
    = rvtt_get_insn_data (rvtt_insn_data::sfpor);
  const rvtt_insn_data *zero_insnd
    = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);
  gcc_assert (cmp_insnd->decl && and_insnd->decl && or_insnd->decl
	      && zero_insnd->decl);

  tree vec_type = TREE_TYPE (g->x);
  gimple_stmt_iterator at = gsi_for_stmt (g->assign);
  location_t loc = gimple_location (g->assign);

  auto build_creg_zero = [&] () -> tree
    {
      gcall *zero = gimple_build_call (zero_insnd->decl, 1,
				       build_int_cst (unsigned_type_node,
						      CREG_IDX_0));
      tree ssa = make_ssa_name (vec_type);
      gimple_call_set_lhs (zero, ssa);
      gimple_set_location (zero, loc);
      gsi_insert_before (&at, zero, GSI_SAME_STMT);
      return ssa;
    };
  auto build_cmp = [&] (bool swap_operands, tree zero_ssa) -> tree
    {
      gcall *cmp = swap_operands
	? gimple_build_call (cmp_insnd->decl, 3, zero_ssa, g->x,
			     build_int_cst (unsigned_type_node,
					    SFPGTLE_MOD1_SET_DEST))
	: gimple_build_call (cmp_insnd->decl, 3, g->x, zero_ssa,
			     build_int_cst (unsigned_type_node,
					    SFPGTLE_MOD1_SET_DEST));
      tree m = make_ssa_name (vec_type);
      gimple_call_set_lhs (cmp, m);
      gimple_set_location (cmp, loc);
      gsi_insert_before (&at, cmp, GSI_SAME_STMT);
      return m;
    };

  tree mask;
  if (eqne)
    {
      tree m_direct = build_cmp (false, build_creg_zero ());
      tree m_swapped = build_cmp (true, g->zv);
      gcall *comb = gimple_build_call ((g->cc == SFPXCMP_MOD1_CC_EQ
					? or_insnd : and_insnd)->decl,
				       2, m_direct, m_swapped);
      mask = make_ssa_name (vec_type);
      gimple_call_set_lhs (comb, mask);
      gimple_set_location (comb, loc);
      gsi_insert_before (&at, comb, GSI_SAME_STMT);
    }
  else if (swapped)
    /* The region's zero is defined before the assign, so it is
       available here.  */
    mask = build_cmp (true, g->zv);
  else
    mask = build_cmp (false, build_creg_zero ());

  gcall *land = gimple_build_call (and_insnd->decl, 2, g->z, mask);
  gimple_call_set_lhs (land, gimple_call_lhs (g->assign));
  gimple_set_location (land, loc);
  gsi_replace (&at, land, false);

  if (dump_file)
    {
      fprintf (dump_file, "ccmask: folded zeroing CC region into ");
      print_gimple_stmt (dump_file, SSA_NAME_DEF_STMT (mask), 0);
      fprintf (dump_file, "ccmask:   masking ");
      print_gimple_stmt (dump_file, land, 0);
    }

  /* Find the zero materialization before deleting its uses, since
     rvtt_prep_stmt_for_deletion can release the SSA name of a single-use
     definition.  */
  gimple *zdef = nullptr;
  if (TREE_CODE (g->zv) == SSA_NAME)
    {
      gimple *d = SSA_NAME_DEF_STMT (g->zv);
      if (d && rvtt_get_insn_data (d))
	zdef = d;
    }

  auto remove = [] (gimple *stmt)
    {
      rvtt_prep_stmt_for_deletion (stmt);
      unlink_stmt_vdef (stmt);
      gimple_stmt_iterator gsi = gsi_for_stmt (stmt);
      gsi_remove (&gsi, true);
      release_defs (stmt);
    };
  remove (g->end);
  remove (g->condb);
  remove (g->fcmp);
  remove (g->xvif);
  remove (g->pushc);
  remove (g->popc);
  /* Delete the now-dead zero so the invariant pass does not try to
     hoist it.  */
  if (zdef && gimple_bb (zdef))
    {
      tree lhs = gimple_call_lhs (zdef);
      if (!lhs || (TREE_CODE (lhs) == SSA_NAME && has_zero_uses (lhs)))
	remove (zdef);
    }

  n_folded++;
}

/* The Blackhole sign-magnitude to two's-complement conversion region:

       sfppushc (0)
       tok = sfpxpred (IF|PUSH, depth 1)
       c   = sfpxcmp (x, 0, TYPE_INT|CC_LT)
       sfpxcond (0, tok, c)
       s   = sfpsetsgn_i (ib, x, 0, 0, 0, 0)
       x1  = sfpassign_lv (x, s)
       n   = sfpiadd_v (x1, 0, ARG_2SCOMP_LREG_DST|CC_NONE)	-- 0 - x1
       x2  = sfpassign_lv (x1, n)
       sfpxpred (depth 1, c)
       sfppopc (0)

   with zero materializations anywhere inside.  It folds to

       a  = SFPABS (x, INT)       -- 0 - x on sign-set lanes
       t  = SFPXOR (x, a)
       x' = SFPSETSGN (a, t)      -- a's low 31 bits, t's sign

   SFPABS leaves 0x80000000 unchanged and maps every other sign-set x to
   a sign-clear value, so t's sign is set exactly when x is a nonzero
   negative.  Those lanes get 0 - |x|, sign-magnitude -0 gets 0 (as the
   sfpi helper requires; SFPCAST would give INT32_MIN), and sign-clear x
   is unchanged.  The XOR and SETSGN destinations are tied to x, so lanes
   disabled by an enclosing v_if keep x.  Near misses are not
   reported.  */

struct ccmask_smag_group
{
  gcall *pushc, *xvif, *icmp, *condb, *setsgn, *assign1, *iadd, *assign;
  gcall *end, *popc;
  tree x;
};

/* Match the conversion region starting at the sfppushc at GSI into G,
   checking it against the CC-region tree CCR.  */

static bool
match_smag_group (const rvtt_cc_region_tree *ccr, gimple_stmt_iterator gsi,
		  ccmask_smag_group *g)
{
  enum { WANT_XVIF, WANT_ICMP, WANT_CONDB, WANT_SETSGN, WANT_ASSIGN1,
	 WANT_IADD, WANT_ASSIGN, WANT_END, WANT_POPC }
    want = WANT_XVIF;

  memset (g, 0, sizeof (*g));
  g->pushc = rvtt_call_with_id (gsi_stmt (gsi), rvtt_insn_data::sfppushc);
  if (!g->pushc || rvtt_call_int_arg (g->pushc, 0) != 0)
    return false;

  gsi_next (&gsi);
  for (unsigned steps = 0; !gsi_end_p (gsi) && steps < 64; gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL)
	continue;
      steps++;

      if (gimple_code (stmt) == GIMPLE_ASSIGN)
	{
	  tree lhs = gimple_get_lhs (stmt);
	  if (lhs && TREE_CODE (lhs) == SSA_NAME
	      && !VECTOR_TYPE_P (TREE_TYPE (lhs)))
	    continue;
	  return false;
	}

      const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
      if (!insnd)
	return false;
      gcall *call = as_a <gcall *> (stmt);

      switch (insnd->id)
	{
	case rvtt_insn_data::sfpxpred:
	  if (want == WANT_XVIF
	      && rvtt_call_int_arg (call, 0)
		 == (SFPXPRED_MOD1_IF | SFPXPRED_MOD1_PUSH
		     | (1u << SFPXPRED_MOD1_DEPTH_SHIFT))
	      && integer_zerop (gimple_call_arg (call, 1)))
	    {
	      g->xvif = call;
	      want = WANT_ICMP;
	      continue;
	    }
	  if (want == WANT_END
	      && rvtt_call_int_arg (call, 0)
		 == (1u << SFPXPRED_MOD1_DEPTH_SHIFT)
	      && gimple_call_arg (call, 1) == gimple_call_lhs (g->condb))
	    {
	      g->end = call;
	      want = WANT_POPC;
	      continue;
	    }
	  return false;

	case rvtt_insn_data::sfpxcmp:
	  {
	    /* Signed x < 0 with x first: the raw sign-bit test.  */
	    tree value;
	    uint32_t cst;
	    if (want != WANT_ICMP
		|| !rvtt_cmp_value_and_cst (call, &value, &cst)
		|| cst != 0 || gimple_call_arg (call, 0) != value
		|| rvtt_call_int_arg (call, insnd->mod_arg ())
		   != (long) ((SFPXCMP_MOD1_TYPE_INT
			       << SFPXCMP_MOD1_TYPE_SHIFT)
			      | SFPXCMP_MOD1_CC_LT))
	      return false;
	    g->icmp = call;
	    g->x = value;
	    want = WANT_CONDB;
	    continue;
	  }

	case rvtt_insn_data::sfpxcond:
	  {
	    if (want != WANT_CONDB)
	      return false;
	    tree c = gimple_call_arg (call, insnd->mod_arg () + 2);
	    tree t = gimple_call_arg (call, insnd->mod_arg () + 1);
	    if (TREE_CODE (c) != SSA_NAME || TREE_CODE (t) != SSA_NAME
		|| SSA_NAME_DEF_STMT (c) != g->icmp
		|| SSA_NAME_DEF_STMT (t) != g->xvif
		|| !has_single_use (c) || !has_single_use (t))
	      return false;
	    g->condb = call;
	    want = WANT_SETSGN;
	    continue;
	  }

	case rvtt_insn_data::sfpsetsgn_i:
	  /* (ib, src, imm, 0, 0, mod1): clear the sign of x.  */
	  if (want != WANT_SETSGN
	      || gimple_call_arg (call, 1) != g->x
	      || rvtt_call_int_arg (call, 2) != 0
	      || rvtt_call_int_arg (call, 3) != 0
	      || rvtt_call_int_arg (call, 4) != 0
	      || rvtt_call_int_arg (call, 5) != 0
	      || !has_single_use (gimple_call_lhs (call)))
	    return false;
	  g->setsgn = call;
	  want = WANT_ASSIGN1;
	  continue;

	case rvtt_insn_data::sfpassign_lv:
	  if (want == WANT_ASSIGN1
	      && gimple_call_arg (call, 0) == g->x
	      && gimple_call_arg (call, 1) == gimple_call_lhs (g->setsgn))
	    {
	      g->assign1 = call;
	      want = WANT_IADD;
	      continue;
	    }
	  if (want == WANT_ASSIGN
	      && gimple_call_arg (call, 0) == gimple_call_lhs (g->assign1)
	      && gimple_call_arg (call, 1) == gimple_call_lhs (g->iadd))
	    {
	      g->assign = call;
	      want = WANT_END;
	      continue;
	    }
	  return false;

	case rvtt_insn_data::sfpiadd_v:
	  /* sfpiadd_v (A, B, ARG_2SCOMP_LREG_DST) computes B - A.  */
	  if (want != WANT_IADD
	      || gimple_call_arg (call, 0) != gimple_call_lhs (g->assign1)
	      || !rvtt_zero_vector_p (gimple_call_arg (call, 1))
	      || rvtt_call_int_arg (call, 2)
		 != (long) (SFPIADD_MOD1_ARG_2SCOMP_LREG_DST
			    | SFPIADD_MOD1_CC_NONE)
	      || !has_single_use (gimple_call_lhs (call)))
	    return false;
	  g->iadd = call;
	  want = WANT_ASSIGN;
	  continue;

	case rvtt_insn_data::sfppopc:
	  if (want != WANT_POPC || rvtt_call_int_arg (call, 0) != 0)
	    return false;
	  g->popc = call;
	  goto region_closed;

	case rvtt_insn_data::sfpxloadi:
	case rvtt_insn_data::sfploadi:
	case rvtt_insn_data::sfploadi_lv:
	case rvtt_insn_data::sfpreadlreg:
	  continue;

	default:
	  return false;
	}
    }
  return false;

 region_closed:
  if (TREE_CODE (g->x) != SSA_NAME)
    return false;
  /* The intermediate merge feeds only the subtract and the final merge;
     the fold removes it.  */
  {
    imm_use_iterator iter;
    use_operand_p use_p;
    FOR_EACH_IMM_USE_FAST (use_p, iter, gimple_call_lhs (g->assign1))
      {
	gimple *use = USE_STMT (use_p);
	if (!is_gimple_debug (use) && use != g->iadd && use != g->assign)
	  return false;
      }
  }
  rvtt_cc_region *r = ccr->region_opened_by (g->pushc);
  return (r && ccr->refinements_pure_p (r)
	  && ccr->region_of (g->assign) == r
	  && r->exits.length () == 1 && r->exits[0] == g->popc);
}

/* Replace the conversion region matched in G.  */

static void
transform_smag_group (ccmask_smag_group *g)
{
  const rvtt_insn_data *abs_insnd
    = rvtt_get_insn_data (rvtt_insn_data::sfpabs);
  const rvtt_insn_data *xor_insnd
    = rvtt_get_insn_data (rvtt_insn_data::sfpxor);
  const rvtt_insn_data *sgn_insnd
    = rvtt_get_insn_data (rvtt_insn_data::sfpsetsgn_v);
  gcc_assert (abs_insnd->decl && xor_insnd->decl && sgn_insnd->decl);

  tree vec_type = TREE_TYPE (g->x);
  gimple_stmt_iterator at = gsi_for_stmt (g->assign);
  location_t loc = gimple_location (g->assign);

  gcall *abscall
    = gimple_build_call (abs_insnd->decl, 2, g->x,
			 build_int_cst (unsigned_type_node, SFPABS_MOD1_INT));
  tree a = make_ssa_name (vec_type);
  gimple_call_set_lhs (abscall, a);
  gimple_set_location (abscall, loc);
  gsi_insert_before (&at, abscall, GSI_SAME_STMT);

  gcall *xorcall = gimple_build_call (xor_insnd->decl, 2, g->x, a);
  tree t = make_ssa_name (vec_type);
  gimple_call_set_lhs (xorcall, t);
  gimple_set_location (xorcall, loc);
  gsi_insert_before (&at, xorcall, GSI_SAME_STMT);

  gcall *sgncall
    = gimple_build_call (sgn_insnd->decl, 3, a, t,
			 build_int_cst (unsigned_type_node,
					SFPSETSGN_MOD1_LREG));
  gimple_call_set_lhs (sgncall, gimple_call_lhs (g->assign));
  gimple_set_location (sgncall, loc);
  gsi_replace (&at, sgncall, false);

  if (dump_file)
    {
      fprintf (dump_file, "ccmask: folded sign-magnitude conversion into ");
      print_gimple_stmt (dump_file, sgncall, 0);
    }

  /* Find the zero materializations before their uses go away.  */
  gimple *zdefs[2] = { nullptr, nullptr };
  tree zeros[2] = { gimple_call_arg (g->icmp, 1),
		    gimple_call_arg (g->iadd, 1) };
  for (unsigned i = 0; i < 2; i++)
    if (TREE_CODE (zeros[i]) == SSA_NAME)
      {
	gimple *d = SSA_NAME_DEF_STMT (zeros[i]);
	if (d && rvtt_get_insn_data (d))
	  zdefs[i] = d;
      }

  auto remove = [] (gimple *stmt)
    {
      rvtt_prep_stmt_for_deletion (stmt);
      unlink_stmt_vdef (stmt);
      gimple_stmt_iterator gsi = gsi_for_stmt (stmt);
      gsi_remove (&gsi, true);
      release_defs (stmt);
    };
  remove (g->iadd);
  remove (g->assign1);
  remove (g->setsgn);
  remove (g->end);
  remove (g->condb);
  remove (g->icmp);
  remove (g->xvif);
  remove (g->pushc);
  remove (g->popc);
  for (unsigned i = 0; i < 2; i++)
    if (zdefs[i] && gimple_bb (zdefs[i])
	&& (i == 0 || zdefs[i] != zdefs[0]))
      {
	tree lhs = gimple_call_lhs (zdefs[i]);
	if (!lhs || (TREE_CODE (lhs) == SSA_NAME && has_zero_uses (lhs)))
	  remove (zdefs[i]);
      }

  n_folded++;
}

/* Fold every matched CC region of FUN opened by an sfppushc.  Return
   whether the IL changed.  */

static bool
transform (function *fun)
{
  bool changed = false;
  basic_block bb;
  /* Each fold deletes a whole leaf frame and inserts only CC-inert
     statements, so the CC-region tree stays valid without a rebuild.  */
  rvtt_cc_region_tree ccr (fun);
  FOR_EACH_BB_FN (bb, fun)
    {
      gimple_stmt_iterator gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gimple_stmt_iterator next = gsi;
	  gsi_next (&next);
	  if (gcall *pushc = rvtt_call_with_id (gsi_stmt (gsi),
					   rvtt_insn_data::sfppushc))
	    {
	      ccmask_group g;
	      bool candidate;
	      if (match_group (&ccr, gsi, &g, &candidate))
		{
		  transform_group (&g);
		  changed = true;
		  /* Continue from NEXT, which is not in the region.  */
		}
	      else if (riscv_tt_opt_cc_region_general > 0
		       && rvtt_call_int_arg (pushc, 0) == 0
		       && match_group_general (fun, &ccr, pushc, &g,
					       &candidate))
		{
		  transform_group (&g);
		  changed = true;
		}
	      else
		{
		  ccmask_smag_group sg;
		  if (match_smag_group (&ccr, gsi, &sg))
		    {
		      transform_smag_group (&sg);
		      changed = true;
		    }
		}
	    }
	  gsi = next;
	}
    }

  return changed;
}

const pass_data pass_data_rvtt_ccmask =
{
  GIMPLE_PASS,
  "rvtt_ccmask",
  OPTGROUP_OTHER,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_rvtt_ccmask : public gimple_opt_pass
{
public:
  pass_rvtt_ccmask (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_ccmask, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_ccmask > 0;
  }

  unsigned execute (function *fn) final override
  {
    /* SFPGT/SFPLE only exist on Blackhole.  */
    if (!TARGET_XTT_TENSIX_BH)
      {
	rvtt_refuse (RVTT_REF_CCMASK_TARGET_UNPROVEN, dump_file,
		     "ccmask refused (ccmask-target-unproven)\n");
	return 0;
      }
    n_folded = 0;
    /* match_group_general needs dominance information.  */
    if (riscv_tt_opt_cc_region_general > 0
	&& !dom_info_available_p (CDI_DOMINATORS))
      calculate_dominance_info (CDI_DOMINATORS);
    bool changed = transform (fn);
    if (dump_file)
      fprintf (dump_file, "ccmask: folds=%u\n", n_folded);
    return changed ? TODO_update_ssa_only_virtuals : 0;
  }
};

} /* anonymous namespace */

/* Create the CC-mask folding pass for CTXT.  */

gimple_opt_pass *
make_pass_rvtt_ccmask (gcc::context *ctxt)
{
  return new pass_rvtt_ccmask (ctxt);
}
