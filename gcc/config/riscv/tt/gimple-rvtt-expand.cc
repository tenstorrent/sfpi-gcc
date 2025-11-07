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
#include "tree-into-ssa.h"
#include "rvtt.h"
#include <unordered_map>

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

static void process_tree(gcall *stmt, gcall *parent);
static void process_tree_node(gimple_stmt_iterator *pre_gsip, gimple_stmt_iterator *post_gsip,
			      bool *negated, gcall *stmt, gcall *parent, bool negate);

static std::unordered_map<gcall *, bool> vif_stmts;
static std::unordered_map<gcall *, bool> phi_stmts;

static unsigned
get_int_arg (gcall *stmt, unsigned int arg)
{
  return TREE_INT_CST_LOW (gimple_call_arg (stmt, arg));
}

static void
remove_stmt(gimple *g)
{
  rvtt_prep_stmt_for_deletion(g);
  unlink_stmt_vdef(g);
  gimple_stmt_iterator gsi = gsi_for_stmt(g);
  gsi_remove(&gsi, true);
  release_defs(g);
}

static int
negate_cmp_mod(int mod)
{
    int op = mod & SFPXCMP_MOD1_CC_MASK;
    int new_op = 0;

    switch (op) {
    case SFPXCMP_MOD1_CC_LT:
	new_op = SFPXCMP_MOD1_CC_GTE;
	break;
    case SFPXCMP_MOD1_CC_NE:
	new_op = SFPXCMP_MOD1_CC_EQ;
	break;
    case SFPXCMP_MOD1_CC_GTE:
	new_op = SFPXCMP_MOD1_CC_LT;
	break;
    case SFPXCMP_MOD1_CC_EQ:
	new_op = SFPXCMP_MOD1_CC_NE;
	break;
    case SFPXCMP_MOD1_CC_LTE:
	new_op = SFPXCMP_MOD1_CC_GT;
	break;
    case SFPXCMP_MOD1_CC_GT:
	new_op = SFPXCMP_MOD1_CC_LTE;
	break;
    }

    return (mod & ~SFPXCMP_MOD1_CC_MASK) | new_op;
}

static bool
cmp_issues_compc(int mod)
{
  return (mod & SFPXCMP_MOD1_CC_MASK) == SFPXCMP_MOD1_CC_LTE;
}

static int get_bool_type(int op, bool negate)
{
  if (op == SFPXBOOL_MOD1_OR)
    return negate ? SFPXBOOL_MOD1_AND : SFPXBOOL_MOD1_OR;
  if (op == SFPXBOOL_MOD1_AND)
    return negate ? SFPXBOOL_MOD1_OR : SFPXBOOL_MOD1_AND;
  gcc_unreachable ();
}

static int
flip_negated_cmp(gcall *stmt, const rvtt_insn_data *insnd, bool negate)
{
  int mod = get_int_arg(stmt, insnd->mod_pos);
  if (negate)
    {
      // Flip the operation
      mod = negate_cmp_mod(mod);
      gimple_call_set_arg(stmt, insnd->mod_pos, build_int_cst(integer_type_node, mod));
    }

  return mod;
}

static gcall*
copy_and_replace_icmp(gcall *stmt, rvtt_insn_data::insn_id id)
{
  const rvtt_insn_data *new_insnd = rvtt_get_insn_data(id);
  int nargs = gimple_call_num_args(stmt);
  gcall * new_stmt = gimple_build_call(new_insnd->decl, nargs);
  for (int i = 0; i < nargs; i++)
    {
      gimple_call_set_arg(new_stmt, i, gimple_call_arg(stmt, i));
    }
  gimple_set_location(new_stmt, gimple_location(stmt));
  // The icmp returns an int used in the boolean tree, the iadd return nothing
  gimple_call_set_lhs(new_stmt, NULL_TREE);
  gimple_set_vuse(new_stmt, gimple_vuse(stmt));
  gimple_stmt_iterator gsi = gsi_for_stmt(stmt);
  gsi_insert_before(&gsi, new_stmt, GSI_SAME_STMT);
  remove_stmt(stmt);

  // Make the iadd do a subtract for the compare
  // Make sure other code knows this is a compare
  int mod = get_int_arg(new_stmt, new_insnd->mod_pos) | SFPXIADD_MOD1_IS_SUB;
  if (id == rvtt_insn_data::sfpxiadd_i)
    {
      mod |= SFPXIADD_MOD1_DST_UNUSED;
    }
  gimple_call_set_arg(new_stmt, new_insnd->mod_pos, build_int_cst(integer_type_node, mod));

  return new_stmt;
}

static void
finish_new_insn(gimple_stmt_iterator *gsip, bool insert_before, gimple *new_stmt, gcall *stmt)
{
  gcc_assert(new_stmt != nullptr);
  gimple_set_location (new_stmt, gimple_location (stmt));
  update_stmt (new_stmt);
  if (insert_before)
    {
      gsi_insert_before(gsip, new_stmt, GSI_NEW_STMT);
    }
  else
    {
      gsi_insert_after(gsip, new_stmt, GSI_NEW_STMT);
    }
}

static void
emit_pushc(gimple_stmt_iterator *gsip, gcall *stmt, bool insert_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfppushc);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 1,
				       build_int_cst (unsigned_type_node, SFPPUSHCC_MOD1_PUSH));
  finish_new_insn(gsip, insert_before, new_stmt, stmt);
}

static void
emit_popc(gimple_stmt_iterator *gsip, gcall *stmt, bool insert_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfppopc);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 1,
				       build_int_cst (unsigned_type_node, SFPPOPCC_MOD1_POP));
  finish_new_insn(gsip, insert_before, new_stmt, stmt);
}

static void
emit_compc(gimple_stmt_iterator *gsip, gcall *stmt, bool emit_before)
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
    rvtt_get_insn_data(rvtt_insn_data::sfpxloadi);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 5, null_pointer_node,
				       build_int_cst (unsigned_type_node, SFPLOADI_MOD0_SHORT),
				       build_int_cst (unsigned_type_node, val),
				       integer_zero_node, integer_zero_node);

  tree tmp = make_ssa_name (build_vector_type(float_type_node, 64), new_stmt);
  gimple_call_set_lhs (new_stmt, tmp);

  finish_new_insn(gsip, emit_before, new_stmt, stmt);

  return tmp;
}

static tree
emit_loadi_lv(gimple_stmt_iterator *gsip, gcall *stmt, tree lhs, tree in, int val, bool emit_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfpxloadi_lv);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 6, null_pointer_node, in,
				       build_int_cst (unsigned_type_node, SFPLOADI_MOD0_SHORT),
				       integer_zero_node, integer_zero_node, integer_zero_node);
  if (lhs == NULL_TREE)
    lhs = make_ssa_name (build_vector_type(float_type_node, 64), new_stmt);
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

static gcall *
find_top_of_cond_tree(gcall *stmt)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data(stmt);

  switch (insnd->id)
    {
    case rvtt_insn_data::sfpxfcmps:
    case rvtt_insn_data::sfpxfcmpv:
    case rvtt_insn_data::sfpxicmps:
    case rvtt_insn_data::sfpxicmpv:
      break;

    case rvtt_insn_data::sfpxbool:
      {
	// Follow only child for NOT, left-most child for AND/OR, all degenerate to same case
	gcall *child = dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXBOOL_LEFT_TREE_ARG_POS)));
	return find_top_of_cond_tree(child);
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
      gcall* stmt;
      const rvtt_insn_data *insnd;
      if (rvtt_p(&insnd, &stmt, top))
	{
	  if (vif_stmts.find(stmt) == vif_stmts.end())
	    {
	      vif_stmts.insert({stmt, true});
	    }
	  else
	    {
	      DUMP("  already processed these stmts, bailing out\n");
	      return;
	    }
	}

      gsi_next(&top);
    }

  if (gsi_end_p(top))
    {
      // Optimizing CCs split across BBs opens up a lot of cases, bail for now
      DUMP("  didn't find xvif in same bb as xcondb, bailing out of optimization\n");
#if 0
      DUMP("  didn't find xvif in same bb as xcondb, processing BBs\n");
      basic_block bb = gsi_bb(top);
      edge_iterator ei;
      edge e;
      FOR_EACH_EDGE(e, ei, bb->succs)
	{
	  gimple_stmt_iterator next_gsi = gsi_start_bb (e->dest);
	  mark_vif_stmts(vif_stmts, next_gsi, bot);
	}
#endif
    }
}

// Expand xcondi into:
//  loadi(0)
//  pushc
//  loadi(1)
//  popc
// Returns results of loadi back to the same SSA as the xcondi for testing, up
//  to the caller to adjust the test as needed (compare against 0)
static void
expand_xcondi(gcall *stmt)
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
static void
process_bool_tree(gimple_stmt_iterator *pre_gsip, gimple_stmt_iterator *post_gsip,
		  bool *negated, gcall *stmt, int op, bool negate)
{
  DUMP("    process %s n:%d\n", op == SFPXBOOL_MOD1_AND ? "AND" : "OR", negate);

  bool negate_node = false;
  if (get_bool_type(op, negate) == SFPXBOOL_MOD1_OR)
    {
      negate_node = true;
      negate = !negate;
    }

  // Emit LEFT
  gimple_stmt_iterator left_post_gsi;
  bool left_negated = false;
  DUMP("    left\n");
  process_tree_node(pre_gsip, &left_post_gsi, &left_negated,
		    dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXBOOL_LEFT_TREE_ARG_POS))),
		    stmt, negate);

  // Emit RIGHT
  gimple_stmt_iterator right_pre_gsi;
  bool right_negated = false;
  DUMP("    right\n");
  process_tree_node(&right_pre_gsi, post_gsip, &right_negated,
		    dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXBOOL_RIGHT_TREE_ARG_POS))),
		    stmt, negate);

  if (right_negated)
    {
      DUMP("	right negated, emitting pre/post\n");
      emit_pushc(&right_pre_gsi, stmt, true);
      tree saved_enables = emit_loadi(&right_pre_gsi, stmt, 1, true);
      saved_enables = emit_loadi_lv(post_gsip, stmt, NULL_TREE, saved_enables, 0, false);
      emit_popc(post_gsip, stmt, false);
      emit_setcc_v(post_gsip, stmt, saved_enables, false);
    }

  if (negate_node)
    {
      DUMP("	node negated, emitting compc\n");
      *negated = true;
      emit_compc(post_gsip, stmt, false);
    }

  if (left_negated)
    {
      // Parent needs a fence for this node's left and side (if the parent
      // isn't the root)
      *negated = true;
    }

  DUMP("    exiting bool %d %d\n", op, negate);
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
      DUMP("  optimizing away xcondi\n");

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
      DUMP("  expanding xcondi\n");

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
  DUMP("  process tree node phi\n");

  // Don't recurse infinitely on phi nodes
  if (phi_stmts.find(stmt) != phi_stmts.end())
    {
      return;
    }
  phi_stmts.insert({stmt, true});

  // The source of this icmps comes from multiple BBs, traverse them
  for (unsigned int i = 0; i < gimple_phi_num_args (child); i++)
    {
      gimple *origin = SSA_NAME_DEF_STMT(gimple_phi_arg_def(child, i));
      if (origin->code == GIMPLE_PHI)
	{
	  process_tree_phi(stmt, origin);
	}
      else if (origin->code == GIMPLE_CALL)
	{
	  gcall *origin_stmt = dyn_cast<gcall *>(origin);
	  const rvtt_insn_data *origin_insnd;
	  origin_insnd = rvtt_get_insn_data(origin_stmt);
	  if (origin_insnd->id == rvtt_insn_data::sfpxcondi)
	    {
	      process_tree(origin_stmt, stmt);
	    }
	}
    }
}

static void
process_tree_node(gimple_stmt_iterator *pre_gsip, gimple_stmt_iterator *post_gsip,
		  bool *negated,
		  gcall *stmt, gcall *parent,
		  bool negate)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data(stmt);
  DUMP("  process %s n:%d\n", insnd->name, negate);

  switch (insnd->id)
    {
    case rvtt_insn_data::sfpxfcmps:
    case rvtt_insn_data::sfpxfcmpv:
      {
	int mod = flip_negated_cmp(stmt, insnd, negate);
	if (cmp_issues_compc(mod)) *negated = true;
	gimple_call_set_lhs (stmt, NULL_TREE);
	*pre_gsip = *post_gsip = gsi_for_stmt(stmt);
      }
      break;

    case rvtt_insn_data::sfpxicmps:
      {
	// Note: negation happens at the use of these trees below the fall thru
	gimple *child = SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXSCMP_SRC_ARG_POS));
	if (child->code == GIMPLE_PHI)
	  {
	    process_tree_phi(stmt, child);
	  }
	else if (child->code == GIMPLE_CALL) // could be inline asm...
	  {
	    gcall *child_call = dyn_cast<gcall *>(child);
	    const rvtt_insn_data *child_insnd = rvtt_get_insn_data(child_call);
	    if (child_insnd->id == rvtt_insn_data::sfpxcondi)
	      {
		DUMP("  descending to process xcondi before xicmps\n");
		// Process child before fixing up this insn
		if (process_xcondi(child_call, stmt, true))
		  {
		    // Optimized this node away...
		    break;
		  }
	      }
	  }
      }
      // Fall thru

    case rvtt_insn_data::sfpxicmpv:
      {
	// iadd insns return a vector while icmp insns return an int, remap
	int mod = flip_negated_cmp(stmt, insnd, negate);
	if (cmp_issues_compc(mod)) *negated = true;
	stmt = copy_and_replace_icmp(stmt, (insnd->id == rvtt_insn_data::sfpxicmps) ?
				     rvtt_insn_data::sfpxiadd_i : rvtt_insn_data::sfpxiadd_v);
	*pre_gsip = *post_gsip = gsi_for_stmt(stmt);
      }
      break;

    case rvtt_insn_data::sfpxbool:
      {
	  int op = get_int_arg(stmt, 0);
	  if (op == SFPXBOOL_MOD1_NOT)
	    {
	      process_tree_node(pre_gsip, post_gsip, negated,
				dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, 1))), stmt, !negate);
	    }
	  else
	    {
	      process_bool_tree(pre_gsip, post_gsip, negated, stmt, op, negate);
	    }
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
}

static void
process_tree(gcall *stmt, gcall *parent)
{
  bool negated = false;
  gimple_stmt_iterator pre_gsi, post_gsi;

  process_tree_node(&pre_gsi, &post_gsi, &negated, stmt, parent, false);
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
  DUMP("Expand pass on: %s\n", function_name(fun));

  phi_stmts.reserve(20);
  vif_stmts.reserve(20);
  basic_block bb;
  gimple_stmt_iterator gsi;

  // Must process xcondis in all BBs before xcondbs because vif stmts can fall
  // in a BB other than the one containing the associated xcondb
  FOR_EACH_BB_FN (bb, fun)
    {
      DUMP("  bb process vif loop\n");
      gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gimple_stmt_iterator next_gsi = gsi;
	  gsi_next(&next_gsi);

	  gcall *stmt;
	  const rvtt_insn_data *insnd;
	  if (rvtt_p(&insnd, &stmt, gsi) &&
	      insnd->id == rvtt_insn_data::sfpxcondb)
	    {
	      DUMP("  process xcondb\n");
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
      DUMP("  bb process outside vif loop\n");
      gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gimple_stmt_iterator next_gsi = gsi;
	  gsi_next(&next_gsi);

	  gcall *stmt;
	  const rvtt_insn_data *insnd;
	  if (rvtt_p(&insnd, &stmt, gsi))
	    {
	      if (insnd->id == rvtt_insn_data::sfpxcondi)
		{
		  DUMP("  process xcondi tree\n");
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
