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
      // We're gonna reomplement this per-arch, so not bothering using sfpadd on bh/qsr
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

  if (dump_file)
    {
      fprintf (dump_file, "Deleting compare ");
      print_gimple_stmt (dump_file, cmp, 0);
    }
  unlink_stmt_vdef (cmp);
  gsi_remove (left, true);

  return op == SFPXCMP_MOD1_CC_LE;
}

static gcall *
find_top_of_cond_tree(gcall *stmt)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data(stmt);

  switch (insnd->id)
    {
    case rvtt_insn_data::sfpxcmp:
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
      emit_setcc (rightmost, call, saved_enables, SFPSETCC_MOD1_LREG_EQ0, SFPSETCC_IMM_TYPE_INT, false);
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
    case rvtt_insn_data::sfpxcmp:
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
