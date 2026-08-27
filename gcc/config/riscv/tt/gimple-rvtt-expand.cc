/* Pass to expand (lower) boolean SFPU operators
   Copyright (C) 2022-2026 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).
   Rewritten by Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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
#include "tree-into-ssa.h"
#include "diagnostic-core.h"
#include "rvtt.h"

using pred_list = std::vector<gcall *>;

static bool expand_cond (pred_list &, unsigned &ix,
			 gimple_stmt_iterator *leftmost, gimple_stmt_iterator *rightmost,
			 tree var, gcall *sink, bool negate);

static void
finish_new_insn (gimple_stmt_iterator *gsip, bool insert_before, gimple *new_stmt, gcall *stmt)
{
  gimple_set_location (new_stmt, gimple_location (stmt));
  if (insert_before)
    gsi_insert_before (gsip, new_stmt, GSI_NEW_STMT);
  else
    gsi_insert_after (gsip, new_stmt, GSI_NEW_STMT);
}

static void
emit_pushc (gimple_stmt_iterator *gsip, gcall *stmt, bool insert_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfppushc);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 1,
				       build_int_cst (unsigned_type_node, SFPPUSHCC_MOD1_PUSH));
  finish_new_insn (gsip, insert_before, new_stmt, stmt);
}

static void
emit_popc (gimple_stmt_iterator *gsip, gcall *stmt, bool insert_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfppopc);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 1,
				       build_int_cst (unsigned_type_node, SFPPOPCC_MOD1_POP));
  finish_new_insn(gsip, insert_before, new_stmt, stmt);
}

static void
emit_compc (gimple_stmt_iterator *gsip, gcall *stmt, bool emit_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfpcompc);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 0);
  finish_new_insn(gsip, emit_before, new_stmt, stmt);
}

static tree
emit_loadi(gimple_stmt_iterator *gsip, gcall *stmt, int val, bool emit_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfploadi);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 5, null_pointer_node,
				       build_int_cst (unsigned_type_node, val),
				       integer_zero_node, integer_zero_node,
				       build_int_cst (unsigned_type_node, SFPLOADI_MOD0_SHORT));
  tree tmp = make_ssa_name (TREE_TYPE (TREE_TYPE (new_insnd->decl)), new_stmt);
  gimple_call_set_lhs (new_stmt, tmp);

  finish_new_insn(gsip, emit_before, new_stmt, stmt);

  return tmp;
}

static tree
emit_loadi_lv(gimple_stmt_iterator *gsip, gcall *stmt, tree lhs, tree in, int val, bool emit_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfploadi_lv);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 6, null_pointer_node, in,
				       build_int_cst (unsigned_type_node, val),
				       integer_zero_node, integer_zero_node,
				       build_int_cst (unsigned_type_node, SFPLOADI_MOD0_SHORT));
  if (lhs == NULL_TREE)
    lhs = make_ssa_name (TREE_TYPE (TREE_TYPE (new_insnd->decl)), new_stmt);
  gimple_call_set_lhs (new_stmt, lhs);

  finish_new_insn(gsip, emit_before, new_stmt, stmt);

  return lhs;
}

static void
emit_setcc (gimple_stmt_iterator *gsip, gcall *stmt, tree in,
	    unsigned mod, unsigned type, bool emit_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfpsetcc);
  gimple *new_stmt = gimple_build_call (new_insnd->decl, new_insnd->num_args ());
  gimple_call_set_arg (new_stmt, new_insnd->src_arg (), in);
  gimple_call_set_arg (new_stmt, new_insnd->mod_arg (),
		       build_int_cst (unsigned_type_node, mod));
  if (TARGET_XTT_TENSIX_QSR)
    gimple_call_set_arg (new_stmt, new_insnd->mod_arg () + 1,
			 build_int_cst (unsigned_type_node, type));
  finish_new_insn(gsip, emit_before, new_stmt, stmt);
}

static unsigned
commute_cmp_args (unsigned op, rvtt_arg_info (&args)[2])
{
  std::swap (args[0], args[1]);
  if ((op & ~(SFPXCMP_MOD1_CC_EQ ^ SFPXCMP_MOD1_CC_NE))
      != SFPXCMP_MOD1_CC_EQ)
    op ^= SFPXCMP_MOD1_CC_LT ^ SFPXCMP_MOD1_CC_GT;
  return op;
}

static bool
expand_cmp_using_gtle (gimple_stmt_iterator *, gcall *, rvtt_arg_info (&)[2], unsigned, unsigned)
{
  return false;
}

static bool
expand_cmp_using_sub (gimple_stmt_iterator *right, gcall *cmp, rvtt_arg_info (&args)[2], unsigned op, unsigned type)
{
  // Turn GT/GE to LT/LE to avoid extra insn,
  // Turn 0 EQ/NE A into A EQ/NE 0 to avoid subtract
  if (op >= SFPXCMP_MOD1_CC_GT
      || (op >= SFPXCMP_MOD1_CC_EQ && args[0].is_zero ()))
    op = commute_cmp_args (op, args);

  int setcc_op = op;
  if (!args[1].is_zero ())
    {
      const rvtt_insn_data *sub_insnd = nullptr;
      unsigned sub_mod = 0;
      tree neg1 = nullptr;

      if (type == SFPXCMP_MOD1_TYPE_FLOAT)
	{
	  auto *lreg_insnd = rvtt_get_insn_data(rvtt_insn_data::sfpreadlreg);
	  gcall *lreg_call = gimple_build_call (lreg_insnd->decl, lreg_insnd->num_args ());
	  auto reg = build_int_cst (unsigned_type_node,
				    TARGET_XTT_TENSIX_WH ? CREG_IDX_NEG_1 : CREG_IDX_1);
	  gimple_call_set_arg (lreg_call, 0, reg);
	  neg1 = make_ssa_name (TREE_TYPE (args[0].get_arg ()));
	  gimple_call_set_lhs (lreg_call, neg1);
	  gimple_set_location (lreg_call, gimple_location (cmp));
	  gsi_insert_after (right, lreg_call, GSI_NEW_STMT);

	  sub_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpmad);
	  sub_mod = TARGET_XTT_TENSIX_WH ? 0 : SFPMAD_MOD1_BH_COMPL_A;
	}
      else
	{
	  static const unsigned char iadd_map[] = {
	    SFPIADD_MOD1_CC_LT0,
	    SFPIADD_MOD1_CC_GTE0,
	    SFPIADD_MOD1_CC_NONE,
	    SFPIADD_MOD1_CC_NONE,
	    0xff,
	    0xff,
	  };

	  sub_mod = iadd_map[op];
	  if (sub_mod != SFPIADD_MOD1_CC_NONE)
	    setcc_op = -1;
	  sub_mod |= SFPIADD_MOD1_ARG_2SCOMP_LREG_DST;
	  sub_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpiadd_v);
	}

      auto *sub_call = gimple_build_call (sub_insnd->decl, sub_insnd->num_args ());
      if (neg1)
	gimple_call_set_arg (sub_call, sub_insnd->src_arg (), neg1);
      gimple_call_set_arg (sub_call, sub_insnd->src_arg () + bool (neg1), args[1].get_arg ());
      gimple_call_set_arg (sub_call, sub_insnd->src_arg () + bool (neg1) + 1, args[0].get_arg ());
      gimple_call_set_arg (sub_call, sub_insnd->mod_arg (),
			   build_int_cst (unsigned_type_node, sub_mod));
      if (setcc_op >= 0)
	{
	  auto tmp = make_ssa_name (TREE_TYPE (args[0].get_arg ()));
	  args[0].set_arg (tmp);
	  gimple_set_lhs (sub_call, tmp);
	}
      gimple_set_location (sub_call, gimple_location (cmp));
      gsi_insert_after (right, sub_call, GSI_NEW_STMT);
    }

  if (setcc_op >= 0)
    {
      static const unsigned char setcc_map[] = {
	SFPSETCC_MOD1_LREG_LT0,
	SFPSETCC_MOD1_LREG_GTE0,
	SFPSETCC_MOD1_LREG_EQ0,
	SFPSETCC_MOD1_LREG_NE0,
	0xff,
	0xff,
      };
      emit_setcc (right, cmp, args[0].get_arg (), setcc_map[setcc_op],
		  type == SFPXCMP_MOD1_TYPE_FLOAT
		  ? SFPSETCC_IMM_TYPE_FLOAT : SFPSETCC_IMM_TYPE_INT, false);
    }

  return false;
}

static bool
verify_cond_call (pred_list &preds, unsigned &ix, gcall *call)
{
  if (!preds[0])
    return true; // Already errored

  auto expected = ix < preds.size () ? preds[ix++] : nullptr;
  if (expected == call)
    return false; // OK

  preds[0] = nullptr;
  error_at (gimple_location (call),
	    "unexpected builtin %qD used within predication region",
	    gimple_call_fndecl (call));
  return true;
}

static gcall *
verify_cond_var (tree var, gcall *call)
{
  if (SSA_VAR_P (var))
    if (gcall *call = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (var)))
      return call;

  error_at (gimple_location (call),
	    "operand of %qD call needs to be a variable",
	    gimple_call_fndecl (call));
  return nullptr;
}

static bool
expand_cmp (gimple_stmt_iterator *left, gimple_stmt_iterator *right,
	    gcall *cmp, const rvtt_insn_data *insnd, bool negate)
{
  *left = *right = gsi_for_stmt (cmp);

  unsigned mod = TREE_INT_CST_LOW (gimple_call_arg (cmp, insnd->mod_arg ()));
  unsigned type = (mod >> SFPXCMP_MOD1_TYPE_SHIFT) & SFPXCMP_MOD1_TYPE_MASK;
  unsigned op = mod & SFPXCMP_MOD1_CC_MASK;
  if (negate)
    op ^= SFPXCMP_MOD1_CC_EQ ^ SFPXCMP_MOD1_CC_NE;

  rvtt_arg_info args[2] =
    {{gimple_call_arg (cmp, insnd->src_arg ()), true},
     {gimple_call_arg (cmp, insnd->src_arg () + 1), true}};

  /*
    We have SFPGT & SFPLE, which work on smag, 2's complement and unsigned (arch-depending)
  LT  b > a
  GE  b <= a
  EQ  b <= a && a >= b (avoid clobbering a tmp)
  NE  (a ^ b) is non zero or for signed_zeros types: not (b <= a && a <= b)
  GT  a > b
  LE  a <= b

  if one side is zero:
  LT0  0>a
  GE0  0<=a
  EQ0  a==0 or for signed_zeros as for above
  NE0  a!=0 or for signed_zeros as for above
  GT0  a>0
  LE0  a<=0

  If we do not have SFPGT & SFPLE, then we need to use a subtract.

  For float we do:
  LT  a - b is neg
  GE  a - b is non neg
  EQ  a - b is zero
  NE  a - b is non zero
  GT  b - a neg
  LE  b - a non neg

  These ignore signed_zero
  LT0  a is neg
  GE0  a is non neg
  EQ0  a is zero
  NE0  a is non zero
  GT0  a is non neg and a is non zero
  LE0  not (a is non neg and a is non zero)

  For int and uint we do: (this ignores the overflow problem)
  LT  a - b is neg
  GE  a - b is non neg
  EQ  a - b is zero
  NE  a - b is non zero
  GT  b - a neg
  LE  b - a non neg

  LT0  a is neg (false for uint)
  GE0  a is non neg (true for uint)
  EQ0  a is zero
  NE0  a is non zero
  GT0  a is non neg and a is non zero (just is non zero for uint)
  LE0  not (a is non neg and a is non zero) (just is zero for uint)

  For int and uint correctly we must have both values within 2^31 of eachother,
  do this by checking if the sign bits match or not.  This would be worth doing
  constant folding using the sign of the constant to handle the result in the
  different-signs case..

  0-0  a - b, neg means a < b
  1-1  a - b, neg means a < b
  1-0  int a is < b, uint a is > b
  0-1  int a is > b, uint a is < b

  t = a ^ b, is neg or non-neg
  t = a - b, is neg or non-neg

  txor = a ^ b, ta_b = a - b

  Int:
  LT (txor is non-neg AND ta_b is neg) OR (txor is neg AND a is neg)
  GE (txor is non-neg AND ta_b is non-neg) OR (txor is neg AND a is non-neg)
  GT handle as b LT a
  LE handle as b GE a
  
  UInt:
  LT (txor is non-neg AND ta_b is neg) OR (txor is neg AND a is non-neg)
  GE (txor is non-neg AND ta_b is non-neg) OR (txor is neg AND a is neg)
  GT handle as b LT a
  LE handle as b GE a

  We can apply demorgans here quite simply.


         WH    BH   QSR   TRI
  Float fsub  >,<=  >,<=  >,<=
  SMag   -    >,<=  >,<=  >,<=
  Int   isub  isub  >,<=  >,<=
  UInt  isub  isub  isub  >,<=
  */

  bool negated = false;
#if 0
  if ((TARGET_XTT_TENSIX_QSR && type >= SFPXCMP_MOD1_TYPE_INT)
       || (TARGET_XTT_TENSIX_BH && type >= SFPXCMP_MOD1_TYPE_SMAG))
    negated = expand_cmp_using_gtle (right, cmp, args, op, type);
  else
#endif
    negated = expand_cmp_using_sub (right, cmp, args, op, type);

#if 0
  // direct reimplementation of existing scheme
  static const int map[] = {
    SFPSETCC_MOD1_LREG_LT0,
    SFPSETCC_MOD1_LREG_GTE0,
    SFPSETCC_MOD1_LREG_EQ0,
    SFPSETCC_MOD1_LREG_NE0,
    SFPSETCC_MOD1_LREG_GTE0,
    SFPSETCC_MOD1_LREG_GTE0
  };
  bool fp = type == SFPXCMP_MOD1_TYPE_FLOAT;
  bool zero = args[1].is_zero ();
  gcall *sub = nullptr;
  int late_cc_op = -1;
  if (op == SFPXCMP_MOD1_CC_GT || op == SFPXCMP_MOD1_CC_LE)
    // GT -> GE && NE0 LE -> !(GT && NE0)
    // We can do better on consts that fit directly, or we only have one
    // use of the loadi
    late_cc_op = SFPXCMP_MOD1_CC_NE;

  if (zero)
    ;
  else if (fp)
    {
      // We're gonna reimplement this per-arch, so not bothering using sfpadd on bh/qsr
      auto one = make_ssa_name (TREE_TYPE (args[0].get_arg ()));
      const rvtt_insn_data *new_insnd =
	rvtt_get_insn_data(rvtt_insn_data::sfpreadlreg);
      auto reg = build_int_cst (unsigned_type_node,
				TARGET_XTT_TENSIX_WH ? CREG_IDX_NEG_1 : CREG_IDX_1);
      gcall *read_lreg = gimple_build_call (new_insnd->decl, new_insnd->num_args (), reg);
      gimple_call_set_lhs (read_lreg, one);
      gsi_insert_after (right, read_lreg, GSI_NEW_STMT);

      const rvtt_insn_data *mad_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpmad);
      auto mod =  build_int_cst (unsigned_type_node,
				 TARGET_XTT_TENSIX_WH ? 0 : SFPMAD_MOD1_BH_COMPL_A);
      sub = gimple_build_call (mad_insnd->decl, mad_insnd->num_args (),
			       args[1].get_arg (), one, args[0].get_arg (), mod);
    }
  else
    {
      static const unsigned iadd_map[] = {
	SFPIADD_MOD1_CC_LT0,
	SFPIADD_MOD1_CC_GTE0,
	SFPIADD_MOD1_CC_NONE,
	SFPIADD_MOD1_CC_NONE,
	SFPIADD_MOD1_CC_GTE0,
	SFPIADD_MOD1_CC_GTE0,
      };

      unsigned mod = iadd_map[op];
      auto *iadd_v_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpiadd_v);
      sub = gimple_build_call (iadd_v_insnd->decl, iadd_v_insnd->num_args ());
      gimple_call_set_arg (sub, iadd_v_insnd->src_arg (), args[1].get_arg ());
      gimple_call_set_arg (sub, iadd_v_insnd->src_arg () + 1, args[0].get_arg ());
      gimple_call_set_arg (sub, iadd_v_insnd->mod_arg (),
			   build_int_cst (unsigned_type_node,
					  mod | SFPIADD_MOD1_ARG_2SCOMP_LREG_DST));
      if (mod == SFPIADD_MOD1_CC_NONE)
	late_cc_op = op;
    }

  if (sub)
    {
      if (late_cc_op >= 0 || fp)
	{
	  args[0].set_arg (make_ssa_name (TREE_TYPE (args[0].get_arg ())));
	  gimple_set_lhs (sub, args[0].get_arg ());
	}
      gsi_insert_after (right, sub, GSI_NEW_STMT);
    }

  if (fp || zero)
    emit_setcc (right, cmp, args[0].get_arg (), map[op],
		fp ? SFPSETCC_IMM_TYPE_FLOAT : SFPSETCC_IMM_TYPE_INT, false);

  if (late_cc_op >= 0)
    emit_setcc (right, cmp, args[0].get_arg (), map[late_cc_op],
		fp ? SFPSETCC_IMM_TYPE_FLOAT : SFPSETCC_IMM_TYPE_INT, false);
  negated = op == SFPXCMP_MOD1_CC_LE;
#endif
  return negated;
}

static gcall *
find_top_of_cond_tree(gcall *stmt)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data(stmt);

  switch (insnd->id)
    {
    case rvtt_insn_data::sfpxcmp:
      break;

    case rvtt_insn_data::sfpxlogic:
      {
	// Follow only child for NOT, left-most child for AND/OR, all degenerate to same case
	gcall *child = dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, 1)));
	return find_top_of_cond_tree (child);
      }
      break;

    default:
      fprintf(stderr, "Illegal rvtt builtin found in conditional tree: %s\n", insnd->name);
      gcc_assert(0);
    }

  return stmt;
}

// Handle AND and OR conditionals
//
// Recursively processes a tree of boolean expressions.	 ORs are converted to
// ANDs by negating the children of the current node.  The negation is toggled
// as the tree is traversed to avoid accumulating redundant negations.
//
// Descending the LHS uses the last PUSHC as the "fence" against which a COMPC
// can be issued, however, descending the RHS would mess up the results from
// the LHS w/o a new fence, hence the PUSHC prior to the RHS.  The POPC would
// destroy the results of the RHS and so those results are saved/restored with
// saved_enables.
static bool
expand_logical (pred_list &preds, unsigned &ix,
		gimple_stmt_iterator *leftmost, gimple_stmt_iterator *rightmost,
		gcall *call, const rvtt_insn_data *call_insnd, bool negate)
{
  unsigned op = TREE_INT_CST_LOW (gimple_call_arg (call, call_insnd->mod_arg ()));
  tree lhs = gimple_call_arg (call, call_insnd->mod_arg () + 1);
  
  if (op == SFPXLOGIC_MOD1_NOT)
    return expand_cond (preds, ix, leftmost, rightmost, lhs, call, !negate);

  bool negated = op == (negate ? SFPXLOGIC_MOD1_AND : SFPXLOGIC_MOD1_OR);
  negate ^= negated;

  // Emit LEFT
  gimple_stmt_iterator lhs_rightmost;
  bool left_negated = expand_cond (preds, ix, leftmost, &lhs_rightmost, lhs, call, negate);

  // Emit RIGHT
  gimple_stmt_iterator rhs_leftmost;
  tree rhs = gimple_call_arg (call, call_insnd->mod_arg () + 2);
  bool right_negated = expand_cond (preds, ix, &rhs_leftmost, rightmost, rhs, call, negate);

  if (right_negated)
    {
      emit_pushc (&rhs_leftmost, call, true);
      tree saved_enables = emit_loadi (&rhs_leftmost, call, 1, true);

      saved_enables = emit_loadi_lv (rightmost, call, NULL_TREE, saved_enables, 0, false);
      emit_popc (rightmost, call, false);
      emit_setcc (rightmost, call, saved_enables, SFPSETCC_MOD1_LREG_EQ0, SFPSETCC_IMM_TYPE_INT, false);
    }

  if (negated)
    emit_compc (rightmost, call, false);

  if (left_negated)
    // Parent needs a fence for this node's left and side (if the parent
    // isn't the root)
    negated = true;

  return negated;
}

static bool
expand_cond (pred_list &preds, unsigned &ix,
	     gimple_stmt_iterator *leftmost, gimple_stmt_iterator *rightmost,
	     tree var, gcall *sink, bool negate)
{
  bool negated = false;

  gcall *call = verify_cond_var (var, sink);
  const rvtt_insn_data *insnd = call ? rvtt_get_insn_data (call) : nullptr;
  if (!insnd)
    {
    fail:
      fprintf(stderr, "Illegal rvtt builtin found in conditional tree: %s\n", insnd->name);
      gcc_assert(0);
    }

  switch (insnd->id)
    {
    default:
      goto fail;

    case rvtt_insn_data::sfpxcmp:
      if (expand_cmp (leftmost, rightmost, call, insnd, negate))
	{
	  emit_compc (rightmost, call, false);
	  negated = true;
	}
      break;

    case rvtt_insn_data::sfpxlogic:
      negated = expand_logical (preds, ix, leftmost, rightmost,
				call, insnd, negate);
      break;
    }

  verify_cond_call (preds, ix, call);

  unlink_stmt_vdef (call);
  gimple_stmt_iterator gsi = gsi_for_stmt (call);
  gsi_remove (&gsi, true);

  return negated;
}

// Expand boolean trees
//
// The hardware does not support OR and generates some comparisons (LTE, GE)
// by ANDing others together and issuing a compc.  This requires refactoring
// boolean expressions using De Moragan's laws.	 The root of a tree is anchored
// by an sfpxcondb.  All dependent operations are chained to this by their
// return values.  This pass traverses the tree, more or less deletes it and
// replaces it with one that works w/ the HW.
static unsigned
transform (function *fun)
{
  basic_block bb;
  pred_list preds;
  bool changed = false;

  FOR_EACH_BB_FN (bb, fun)
    {
      for (auto gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
	{
	  auto *insnd = rvtt_get_insn_data (*gsi);
	  if (!insnd)
	    continue;

	  auto *call = as_a <gcall *> (*gsi);
	  switch (insnd->id)
	    {
	    default:
	      if (!preds.empty () && insnd->sets_cc (call))
		error_at (gimple_location (call),
			  "disallowed cc-setting builtin %qD within predication region",
			  gimple_call_fndecl (call));
	      break;

	    case rvtt_insn_data::sfpxpred:
	      if (!preds.empty ())
		{
		  preds.clear ();
		  error_at (gimple_location (call),
			    "Disallowed nested predication region");
		}
	      preds.push_back (call);
	      break;

	    case rvtt_insn_data::sfpxlogic:
	    case rvtt_insn_data::sfpxcmp:
	    case rvtt_insn_data::sfpxcond:
	      if (preds.empty ())
		error_at (gimple_location (call),
			  "predication builtin %qD outside of predication region",
			  gimple_call_fndecl (call));
	      preds.push_back (call);
	      if (insnd->id != rvtt_insn_data::sfpxcond)
		break;

	      unsigned ix = 0;
	      tree pred_var = gimple_call_arg (call, insnd->mod_arg () + 1);

	      gcall *first = verify_cond_var (pred_var, call);
	      if (!first)
		break;
	      verify_cond_call (preds, ix, first);

	      tree cond_var = gimple_call_arg (call, insnd->mod_arg () + 2);
	      gimple_stmt_iterator leftmost, rightmost;

	      expand_cond (preds, ix, &leftmost, &rightmost, cond_var, call, false);

	      verify_cond_call (preds, ix, call);
	      gimple_call_set_arg (call, insnd->mod_arg () + 2, integer_zero_node);
	      update_stmt (call);
	      preds.clear ();
	      changed = true;
	      break;
	    }
	}
      if (!preds.empty ())
	{
	  error_at (gimple_location (*preds.begin ()),
		    "untermated predication region");
	  preds.clear ();
	}
    }

  return changed ? TODO_update_ssa : 0;
}

namespace {

const pass_data pass_data_rvtt_expand =
{
  GIMPLE_PASS, /* type */
  "rvtt_expand", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_expand : public gimple_opt_pass
{
public:
  pass_rvtt_expand (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_expand, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }

  virtual unsigned int execute (function *fn) override
  {
    return transform (fn);
  }
}; // class pass_rvtt_expand

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_expand (gcc::context *ctxt)
{
  return new pass_rvtt_expand (ctxt);
}
