/* Pass to expand (lower) boolean SFPU operators
   Copyright (C) 2022-2025 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).

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
#include "gimple-pretty-print.h"
#include "tree-into-ssa.h"
#include "rvtt.h"
#include <unordered_map>

static void process_tree (gcall *stmt, gcall *parent);
static bool simplify_node (tree node, gimple_stmt_iterator *leftmost, gimple_stmt_iterator *rightmost,
			   gcall *parent, bool negate);

static std::unordered_map<gcall *, bool> vif_stmts;
static std::unordered_map<gcall *, bool> phi_stmts;

static void
remove_stmt(gimple *g)
{
  //  rvtt_prep_stmt_for_deletion(g);
  unlink_stmt_vdef(g);
  gimple_stmt_iterator gsi = gsi_for_stmt(g);
  gsi_remove(&gsi, true);
  //  release_defs(g);
}

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
emit_setcc_v(gimple_stmt_iterator *gsip, gcall *stmt, tree in, bool emit_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfpsetcc_v);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 2, in,
				       build_int_cst (unsigned_type_node, SFPSETCC_MOD1_LREG_EQ0));
  finish_new_insn(gsip, emit_before, new_stmt, stmt);
}

// This is temporary code, and somewhet yucky ...
static void
emit_sfploadi (gimple_stmt_iterator *gsi, tree dst, uint32_t int_imm)
{
  // FIXME: we're just moving bits around here, the type of the input value
  // doesnt matter.
  int new_mod = -1;

  if (int_imm <= 0x7fff || int_imm >= 0xffff8000)
    new_mod = SFPLOADI_MOD0_SHORT;
  else if (int_imm <= 0xffff)
    new_mod = SFPLOADI_MOD0_USHORT;
  else if (!(int_imm & 0xffff))
    {
      new_mod = SFPLOADI_MOD0_FLOATB;
      int_imm >>= 16;
    }
  else if (!(int_imm & 0x1FFF))
    {
      int exp = (int_imm >> 23) & 0xFF;

      if (exp < 127 + 16 && exp >= 127 - 14)
	  {
	    // Fits in fp16a
	    int_imm = ((int_imm >> 13) & 0x3ff)
	      | ((int_imm >> 16) & 0x8000)
	      | ((exp - 0x70) << 10);
	    new_mod = SFPLOADI_MOD0_FLOATA;
	  }
    }

  auto *loadi_insnd = rvtt_get_insn_data (rvtt_insn_data::sfploadi);
  auto *loadi = gimple_build_call (loadi_insnd->decl, loadi_insnd->num_args ());
  gimple_set_lhs (loadi, new_mod < 0 ? make_ssa_name (TREE_TYPE (dst)) : dst);
  gimple_call_set_arg (loadi, 0, null_pointer_node);
  gimple_call_set_arg (loadi, loadi_insnd->imm_arg (),
		       build_int_cst (unsigned_type_node, int_imm & 0xffff));
  gimple_call_set_arg (loadi, loadi_insnd->var_arg (), integer_zero_node);
  gimple_call_set_arg (loadi, loadi_insnd->id_arg (), integer_zero_node);
  gimple_call_set_arg (loadi, loadi_insnd->mod_arg (),
		       build_int_cst (unsigned_type_node, new_mod < 0 ? SFPLOADI_MOD0_USHORT : new_mod));
  gsi_insert_after (gsi, loadi, GSI_NEW_STMT);

  if (new_mod < 0)
    {
      auto *loadi_lv_insnd = rvtt_get_insn_data (rvtt_insn_data::sfploadi_lv);
      auto *loadi_lv = gimple_build_call (loadi_lv_insnd->decl, loadi_lv_insnd->num_args ());
      gimple_set_lhs (loadi_lv, dst);
      gimple_call_set_arg (loadi_lv, 0, null_pointer_node);
      gimple_call_set_arg (loadi_lv, loadi_lv_insnd->live_arg (), gimple_get_lhs (loadi));
      gimple_call_set_arg (loadi_lv, loadi_lv_insnd->imm_arg (),
			   build_int_cst (unsigned_type_node, (int_imm >> 16) & 0xffff));
      gimple_call_set_arg (loadi_lv, loadi_lv_insnd->var_arg (), integer_zero_node);
      gimple_call_set_arg (loadi_lv, loadi_lv_insnd->id_arg (), integer_zero_node);
      gimple_call_set_arg (loadi_lv, loadi_lv_insnd->mod_arg (),
			   build_int_cst (unsigned_type_node, SFPLOADI_MOD0_UPPER));
      gsi_insert_after (gsi, loadi_lv, GSI_NEW_STMT);
    }
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

  struct arg_info
  {
    tree arg;
    gcall *def = nullptr; // loadi def
    tree cst = nullptr; // scalar const
    uint32_t imm = 0; // scalar const

    arg_info (tree arg)
      : arg (arg)
    {
      if (!SSA_VAR_P (arg))
	{
	  cst = arg;
	  imm = TREE_INT_CST_LOW (cst);
	  return;
	}
#if 0 // later ....
      auto *d = SSA_NAME_DEF_STMT (lhs);
      if (auto *insnd = rvtt_get_insn_data (d))
	{
	  auto *call = as_a <gcall *> (d);
	  switch (insnd->id)
	    {
	    default:
	      return;

	    case rvtt_insn_data::sfpreadlreg:
	      // We only care about detecting zero here
	      if (TREE_INT_CST_LOW (gimple_call_arg (call, 0))
		  != CREG_IDX_0)
		return;
	      cst = build_int_cst (unsigned_type_node, 0);
	      return;

	    case rvtt_insn_data::sfploadi:
	      if (!integer_zerop (gimple_call_arg (call, 0)))
		return;
	      bool ushort = false;
	      switch (TREE_INT_CST_LOW (gimple_call_arg (call, insnd->mod_arg ())))
		{
		default:
		  return;

		case SFPLOADI_MOD0_SHORT:
		  ushort = true;
		  break;

		case SFPLOADI_MOD0_USHORT:
		  break;
		}
	      imm = TREE_INT_CST_LOW (gimple_call_arg (call, insnd->imm_arg ()));
	      if (ushort && imm & 0x8000)
		imm |= 0xffff0000;
	      def = d; // Remember this so we can delete or replace it maybe
	      break:
	    }
	}
#endif
    }
  };
  bool is_scalar = insnd->has_var ();
  arg_info args[2] =
    {{gimple_call_arg (cmp, insnd->src_arg ())},
     {gimple_call_arg (cmp, is_scalar ? insnd->imm_arg () : insnd->src_arg () + 1)}};

  // direct reimplementation of existing scheme
  static const int map[] = {
    SFPSETCC_MOD1_LREG_LT0,
    SFPSETCC_MOD1_LREG_GTE0,
    SFPSETCC_MOD1_LREG_EQ0,
    SFPSETCC_MOD1_LREG_NE0,
    SFPSETCC_MOD1_LREG_GTE0,
    SFPSETCC_MOD1_LREG_GTE0
  };
  auto *setcc_v_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpsetcc_v);
  bool integral = type != SFPXCMP_MOD1_TYPE_FLOAT;
  bool needs_sub = true;
  if (args[1].cst)
    {
      if (!args[1].imm || (!integral && args[1].imm == 0x80000000))
	needs_sub = false;
      else if ((args[1].imm & 0x7fffffff) == 0x3f800000)
	{
	  args[1].arg = make_ssa_name (TREE_TYPE (args[0].arg));
	  args[1].cst = nullptr;
	  const rvtt_insn_data *new_insnd =
	    rvtt_get_insn_data(rvtt_insn_data::sfpreadlreg);
	  unsigned cst = args[1].imm >> 31 ? CREG_IDX_NEG_1 : CREG_IDX_1;
	  gcall *read_lreg = gimple_build_call (new_insnd->decl, 1,
						build_int_cst (unsigned_type_node, cst));
	  gimple_call_set_lhs (read_lreg, args[1].arg);
	  gsi_insert_after (right, read_lreg, GSI_NEW_STMT);
	}
      else if (args[1].imm <= 0x800 || args[1].imm > 0xfffff800)
	args[1].cst = build_int_cst (unsigned_type_node, -args[1].imm);
      else
	{
	  args[1].cst = nullptr;
	  args[1].arg = make_ssa_name (TREE_TYPE (args[0].arg));
	  emit_sfploadi (right, args[1].arg, args[1].imm);
	}
    }
  gcall *sub = nullptr;
  int late_cc_op = -1;
  if (op == SFPXCMP_MOD1_CC_GT || op == SFPXCMP_MOD1_CC_LE)
    // GT -> GE && NE0 LE -> !(GT && NE0)
    // We can do better on consts that fit directly, or we only have one
    // use of the loadi
    late_cc_op = SFPXCMP_MOD1_CC_NE;

  if (!needs_sub)
    ;
  else if (!integral)
    {
      auto neg1 = make_ssa_name (TREE_TYPE (args[0].arg));
      const rvtt_insn_data *new_insnd =
	rvtt_get_insn_data(rvtt_insn_data::sfpreadlreg);
      gcall *read_lreg = gimple_build_call (new_insnd->decl, 1,
					    build_int_cst (unsigned_type_node, CREG_IDX_NEG_1));
      gimple_call_set_lhs (read_lreg, neg1);
      gsi_insert_after (right, read_lreg, GSI_NEW_STMT);

      const rvtt_insn_data *mad_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpmad);
      sub = gimple_build_call (mad_insnd->decl, mad_insnd->num_args (),
			       args[1].arg, neg1, args[0].arg, integer_zero_node);
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
      if (args[1].cst)
	{
	  auto *iadd_i_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpiadd_i);
	  sub = gimple_build_call (iadd_i_insnd->decl, iadd_i_insnd->num_args ());
	  gimple_call_set_arg (sub, 0, null_pointer_node);
	  gimple_call_set_arg (sub, iadd_i_insnd->src_arg (), args[0].arg);
	  gimple_call_set_arg (sub, iadd_i_insnd->imm_arg (), args[1].cst);
	  gimple_call_set_arg (sub, iadd_i_insnd->var_arg (), integer_zero_node);
	  gimple_call_set_arg (sub, iadd_i_insnd->id_arg (), integer_zero_node);
	  gimple_call_set_arg (sub, iadd_i_insnd->mod_arg (),
			       build_int_cst (unsigned_type_node, mod));
	}
      else
	{
	  auto *iadd_v_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpiadd_v);
	  sub = gimple_build_call (iadd_v_insnd->decl, iadd_v_insnd->num_args ());
	  gimple_call_set_arg (sub, iadd_v_insnd->src_arg (), args[1].arg);
	  gimple_call_set_arg (sub, iadd_v_insnd->src_arg () + 1, args[0].arg);
	  gimple_call_set_arg (sub, iadd_v_insnd->mod_arg (),
			       build_int_cst (unsigned_type_node,
					      mod | SFPIADD_MOD1_ARG_2SCOMP_LREG_DST));
	}
      if (mod == SFPIADD_MOD1_CC_NONE)
	late_cc_op = op;
    }

  if (sub)
    {
      if (late_cc_op >= 0 || !integral)
	{
	  args[0].arg = make_ssa_name (TREE_TYPE (args[0].arg));
	  gimple_set_lhs (sub, args[0].arg);
	}
      gsi_insert_after (right, sub, GSI_NEW_STMT);
    }

  if (!integral || !needs_sub)
    {
      auto setcc = gimple_build_call (setcc_v_insnd->decl, setcc_v_insnd->num_args ());
      gimple_call_set_arg (setcc, setcc_v_insnd->src_arg (), args[0].arg);
      gimple_call_set_arg (setcc, setcc_v_insnd->mod_arg (),
			   build_int_cst (unsigned_type_node, map[op]));
      gimple_set_location (setcc, gimple_location (cmp));
      gsi_insert_after (right, setcc, GSI_NEW_STMT);
    }

  if (late_cc_op >= 0)
    {
      auto setcc = gimple_build_call (setcc_v_insnd->decl, setcc_v_insnd->num_args ());
      gimple_call_set_arg (setcc, setcc_v_insnd->src_arg (), args[0].arg);
      gimple_call_set_arg (setcc, setcc_v_insnd->mod_arg (),
			   build_int_cst (unsigned_type_node, map[late_cc_op]));
      gimple_set_location (setcc, gimple_location (cmp));
      gsi_insert_after (right, setcc, GSI_NEW_STMT);
    }

  if (dump_file)
    {
      fprintf (dump_file, "Deleting compare ");
      print_gimple_stmt (dump_file, cmp, 0);
    }
  unlink_stmt_vdef (cmp);
  gsi_remove (left, true);

  if (dump_file)
    print_ssa_def_use (dump_file, args[0].arg);

  return op == SFPXCMP_MOD1_CC_LE;
}

static gcall *
find_top_of_cond_tree(gcall *stmt)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data(stmt);

  switch (insnd->id)
    {
    case rvtt_insn_data::sfpxcmps:
    case rvtt_insn_data::sfpxcmpv:
      break;

    case rvtt_insn_data::sfpxbool:
      {
	// Follow only child for NOT, left-most child for AND/OR, all degenerate to same case
	gcall *child = dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXBOOL_LEFT_TREE_ARG_POS)));
	return find_top_of_cond_tree (child);
      }
      break;

    case rvtt_insn_data::sfpxcondi:
      // Should never get this deep
      gcc_assert(0);
      break;

    default:
      fprintf(stderr, "Illegal rvtt builtin found in conditional tree: %s\n", insnd->name);
      gcc_assert(0);
    }

  return stmt;
}

static void
mark_vif_stmts(gimple_stmt_iterator top,
	       gimple_stmt_iterator bot)
{
  while (top.ptr != bot.ptr &&
	 !gsi_end_p(top))
    {
      if (rvtt_get_insn_data (*top))
	{
	  if (vif_stmts.find(as_a <gcall *> (*top)) == vif_stmts.end())
	    vif_stmts.insert({as_a <gcall *> (*top), true});
	  else
	    {
	      if (dump_file)
		fprintf (dump_file, "  already processed these stmts, bailing out\n");
	      return;
	    }
	}

      gsi_next(&top);
    }

  if (gsi_end_p(top))
    // Optimizing CCs split across BBs opens up a lot of cases, bail for now
    if (dump_file)
      fprintf (dump_file, "  didn't find xvif in same bb as xcondb, bailing out of optimization\n");
}

// Expand xcondi into:
//  loadi(0)
//  pushc
//  loadi(1)
//  popc
// Returns results of loadi back to the same SSA as the xcondi for testing, up
//  to the caller to adjust the test as needed (compare against 0)
static void
expand_xcondi (gcall *stmt)
{
  gcall *child = dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXCONDI_TREE_ARG_POS)));
  gcall *top = find_top_of_cond_tree(child);

  gimple_stmt_iterator gsi = gsi_for_stmt(top);
  tree save = emit_loadi(&gsi, top, 0, true);
  emit_pushc(&gsi, top, true);
  gsi = gsi_for_stmt(child);
  tree lhs = gimple_call_lhs(stmt);
  save = emit_loadi_lv(&gsi, top, lhs, save, 1, false);
  emit_popc(&gsi, top, false);

  // Delete the stmt, but not it's DEFs!
  rvtt_prep_stmt_for_deletion(stmt);
  gsi = gsi_for_stmt(stmt);
  gsi_remove(&gsi, true);
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
simplify_logical (gcall *call, gimple_stmt_iterator *leftmost, gimple_stmt_iterator *rightmost, unsigned op, bool negate)
{
  tree lhs = gimple_call_arg (call, 1);
  if (op == SFPXBOOL_MOD1_NOT)
    return simplify_node (lhs, leftmost, rightmost, call, !negate);

  if (dump_file)
    fprintf (dump_file, "    process %s n:%d\n", op == SFPXBOOL_MOD1_AND ? "AND" : "OR", negate);

  bool negated = op == (negate ? SFPXBOOL_MOD1_AND : SFPXBOOL_MOD1_OR);
  negate ^= negated;

  // Emit LEFT
  gimple_stmt_iterator lhs_rightmost;
  if (dump_file)
    fprintf (dump_file, "    left\n");
  bool left_negated = simplify_node (lhs, leftmost, &lhs_rightmost, call, negate);

  // Emit RIGHT
  gimple_stmt_iterator rhs_leftmost;
  if (dump_file)
    fprintf (dump_file, "    right\n");
  bool right_negated = simplify_node (gimple_call_arg(call, 2),
				      &rhs_leftmost, rightmost, call, negate);

  if (right_negated)
    {
      if (dump_file)
	fprintf (dump_file, "	right negated, emitting pre/post\n");

      emit_pushc(&rhs_leftmost, call, true);
      tree saved_enables = emit_loadi(&rhs_leftmost, call, 1, true);

      saved_enables = emit_loadi_lv(rightmost, call, NULL_TREE, saved_enables, 0, false);
      emit_popc(rightmost, call, false);
      emit_setcc_v(rightmost, call, saved_enables, false);
    }

  if (negated)
    {
      if (dump_file)
	fprintf (dump_file, "	node negated, emitting compc\n");

      emit_compc (rightmost, call, false);
    }

  if (left_negated)
    // Parent needs a fence for this node's left and side (if the parent
    // isn't the root)
    negated = true;

  if (dump_file)
    fprintf (dump_file, "    exiting bool %d %d\n", op, negate);

  return negated;
}

static bool
process_xcondi(gcall *stmt, gcall *parent, bool optimizeit)
{
  // Process the child as a new tree
  gcall *child = dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXCONDI_TREE_ARG_POS)));

  bool optimized = false;
  tree cmp_lhs = gimple_call_lhs (parent);
  // These tests are redundant, but may be relevent if more cases are
  // optimized in the future
  if (optimizeit &&
      has_single_use(cmp_lhs) &&
      vif_stmts.find(stmt) != vif_stmts.end())
    {
      if (dump_file)
	fprintf (dump_file, "  optimizing away xcondi\n");

      // Parent is an xicmps, the single vuse is an xcondb, move the
      // conditional into the xcondb and optimize away the xcondi and the
      // associated xicmps
      // Stuff the xcondi arg into the use of the icmps (and xcondi or xcondb)
      tree xcondi_op = gimple_call_arg(stmt, 0);

      gimple *xcondb_stmt;
      use_operand_p use;
      single_imm_use (cmp_lhs, &use, &xcondb_stmt);

      gimple_call_set_arg(xcondb_stmt, 0, xcondi_op);
      update_stmt(xcondb_stmt);

      remove_stmt(parent);
      remove_stmt(stmt);

      optimized = true;
    }
  else
    {
      if (dump_file)
	fprintf (dump_file, "  expanding xcondi\n");

      // The integer conditional comparison falls outside a v_if, can't optimize
      // Instead, save the result in an int to be used later
      expand_xcondi(stmt);
    }

  process_tree(child, stmt);

  return optimized;
}

static void
process_tree_phi(gcall *stmt, gimple *child)
{
  if (dump_file)
    fprintf (dump_file, "  process tree node phi\n");

  // Don't recurse infinitely on phi nodes
  if (phi_stmts.find(stmt) != phi_stmts.end())
    return;
  phi_stmts.insert({stmt, true});

  // The source of this icmps comes from multiple BBs, traverse them
  for (unsigned int i = 0; i < gimple_phi_num_args (child); i++)
    {
      gimple *origin = SSA_NAME_DEF_STMT(gimple_phi_arg_def(child, i));
      if (origin->code == GIMPLE_PHI)
	process_tree_phi(stmt, origin);
      else if (origin->code == GIMPLE_CALL)
	{
	  gcall *origin_stmt = dyn_cast<gcall *>(origin);
	  const rvtt_insn_data *origin_insnd;
	  origin_insnd = rvtt_get_insn_data(origin_stmt);
	  if (origin_insnd->id == rvtt_insn_data::sfpxcondi)
	    process_tree(origin_stmt, stmt);
	}
    }
}

static bool
simplify_node (tree node, gimple_stmt_iterator *leftmost, gimple_stmt_iterator *rightmost,
	       gcall *parent, bool negate)
{
  gcall *stmt = as_a <gcall *> (SSA_NAME_DEF_STMT (node));
  bool negated = false;
  const rvtt_insn_data *insnd = rvtt_get_insn_data(stmt);
  if (dump_file)
    fprintf (dump_file, "  process %s n:%d\n", insnd->name, negate);

  switch (insnd->id)
    {
    case rvtt_insn_data::sfpxcmps:
      {
	// Note: negation happens at the use of these trees below the fall thru
	gimple *child = SSA_NAME_DEF_STMT(gimple_call_arg(stmt, insnd->src_arg ()));
	if (child->code == GIMPLE_PHI)
	  process_tree_phi(stmt, child);
	else if (child->code == GIMPLE_CALL) // could be inline asm...
	  {
	    gcall *child_call = dyn_cast<gcall *>(child);
	    const rvtt_insn_data *child_insnd = rvtt_get_insn_data(child_call);
	    if (child_insnd->id == rvtt_insn_data::sfpxcondi)
	      {
		if (dump_file)
		  fprintf (dump_file, "  descending to process xcondi before xicmps\n");
		// Process child before fixing up this insn
		if (process_xcondi(child_call, stmt, true))
		  // Optimized this node away...
		  break;
	      }
	  }
      }
      // Fall thru

    case rvtt_insn_data::sfpxcmpv:
      if (expand_cmp (leftmost, rightmost, stmt, insnd, negate))
	{
	  emit_compc (rightmost, stmt, false);
	  negated = true;
	}
      break;

    case rvtt_insn_data::sfpxbool:
      {
	negated = simplify_logical (stmt, leftmost, rightmost,
				    TREE_INT_CST_LOW (gimple_call_arg (stmt, insnd->mod_arg ())), negate);
	remove_stmt(stmt);
      }
      break;

    case rvtt_insn_data::sfpxcondi:
      process_xcondi(stmt, parent, false);
      break;

    default:
      fprintf(stderr, "Illegal rvtt builtin found in conditional tree: %s\n", insnd->name);
      gcc_assert(0);
    }

  return negated;
}

static void
process_tree(gcall *stmt, gcall *parent)
{
  gimple_stmt_iterator leftmost, rightmost;

  simplify_node (gimple_call_lhs (stmt), &leftmost, &rightmost, parent, false);
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
  if (dump_file)
    fprintf (dump_file, "Expand pass on: %s\n", function_name(fun));

  phi_stmts.reserve(20);
  vif_stmts.reserve(20);
  basic_block bb;
  gimple_stmt_iterator gsi;

  // Must process xcondis in all BBs before xcondbs because vif stmts can fall
  // in a BB other than the one containing the associated xcondb
  FOR_EACH_BB_FN (bb, fun)
    {
      if (dump_file)
	fprintf (dump_file, "  bb process vif loop\n");
      gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gimple_stmt_iterator next_gsi = gsi;
	  gsi_next(&next_gsi);

	  auto *insnd = rvtt_get_insn_data (*gsi);
	  if (insnd && insnd->id == rvtt_insn_data::sfpxcondb)
	    {
	      auto *stmt = as_a <gcall *> (*gsi);
	      if (dump_file)
		fprintf (dump_file, "  process xcondb\n");
	      // This will be the sfpxvif stmt
	      gcall *child = dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXCONDB_TREE_ARG_POS)));
	      gcall* top = dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXCONDB_START_ARG_POS)));
	      mark_vif_stmts(gsi_for_stmt(top), gsi);

	      process_tree(child, stmt);

	      remove_stmt(stmt);
	      remove_stmt(top);
	      vif_stmts.clear();
	      phi_stmts.clear();
	    }

	  gsi = next_gsi;
	}
    }

  // Now process any xcondis that aren't associated w/ a xcondbs
  FOR_EACH_BB_FN (bb, fun)
    {
      if (dump_file)
	fprintf (dump_file, "  bb process outside vif loop\n");
      gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gimple_stmt_iterator next_gsi = gsi;
	  gsi_next(&next_gsi);

	  if (auto *insnd = rvtt_get_insn_data (*gsi))
	    {
	      if (insnd->id == rvtt_insn_data::sfpxcondi)
		{
		  auto *stmt = as_a <gcall *> (*gsi);
		  if (dump_file)
		    fprintf (dump_file, "  process xcondi tree\n");
		  gcall *child = dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXCONDI_TREE_ARG_POS)));
		  expand_xcondi(stmt);
		  process_tree(child, stmt);
		  phi_stmts.clear();
		}
	    }

	  gsi = next_gsi;
	}
    }

  return TODO_update_ssa;
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
