/* Pass to generate SFPU synth-opcode and opcode synth sequences for
   currently-non-constant operands.
   Copyright (C) 2022-2025 Tenstorrent Inc.
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
#include <unordered_set>
#include <unordered_map>

// This pass finds builtins with non-constant args and:
//  - emit a prologue to synthesize the instruction (shift, mask,
//    synth_opcode, add)
//  - link the synth_opcode to the builtin via a unique id (this id
// also prevents CSE combining unrelated synth_opcodes)
//
// The nonimm_pos field in insnd points to the permissible
// non-constant argument in the builtin. The nonimm_pos+1 argument is
// where the synthesized instruction is added and the nonimmm_pos+2 is
// where the synth_opcode dependency is stored.
//
// This pass runs early in the pipe, but after inlining. Doing that
// allows the prologue to be separated from the use.  After loop
// unrolling, there can be multiple synth_opcodes with the same ids.
// Not all constant propagation is done at this point, so later we can
// discover some builtins have a constant argument (which is one
// reason to leave that argument alone right now). We'll detach the
// prologue at that point.

static gcall *
make_synth (location_t loc, tree id, tree var = nullptr)
{
  auto opcode_insnd = rvtt_get_insn_data (rvtt_insn_data::synth_opcode);
  if (!var)
    var = make_temp_ssa_name (unsigned_type_node, nullptr, "li");
  gcall *stmt = gimple_build_call (opcode_insnd->decl, opcode_insnd->num_args ());
  gimple_call_set_arg (stmt, 0, id);
  gimple_call_set_arg (stmt, 1, integer_zero_node);
  gimple_call_set_lhs (stmt, var);
  gimple_set_location (stmt, loc);

  return stmt;
}

static tree
build_var_synth (gimple_stmt_iterator gsi, const rvtt_insn_data *insnd,
		 location_t loc, tree val, tree id)
{
  uint32_t mask = (uint32_t (1) << insnd->imm_bits ()) - 1;
  tree var_mask = make_temp_ssa_name (unsigned_type_node, nullptr, "mask");
  gimple *stmt_mask = gimple_build_assign (var_mask, BIT_AND_EXPR, val,
					   build_int_cst (unsigned_type_node, mask));
  gimple_set_location (stmt_mask, loc);
  gsi_insert_before (&gsi, stmt_mask, GSI_SAME_STMT);

  tree var_encode = var_mask;
  if (uint32_t shift = insnd->imm_encode ())
    {
      var_encode = make_temp_ssa_name (unsigned_type_node, nullptr, "shift");
      gimple *stmt_encode = gimple_build_assign (var_encode, LSHIFT_EXPR, var_mask,
						 build_int_cst (unsigned_type_node, shift));
      gimple_set_location (stmt_encode, loc);
      gsi_insert_before (&gsi, stmt_encode, GSI_SAME_STMT);
    }

  gimple *opc_stmt = make_synth (loc, id);
  gsi_insert_before (&gsi, opc_stmt, GSI_SAME_STMT);

  tree var_sum = make_temp_ssa_name (unsigned_type_node, nullptr, "sum");
  gimple *stmt_sum = gimple_build_assign (var_sum, PLUS_EXPR, gimple_call_lhs (opc_stmt), var_encode);
  gimple_set_location (stmt_sum, loc);
  gsi_insert_before (&gsi, stmt_sum, GSI_SAME_STMT);

  return var_sum;
}

static unsigned
split (function *fn)
{
  basic_block bb;

  bool updated = false;
  unsigned int synth_id = 1;
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	 !gsi_end_p (gsi); gsi_next (&gsi))
      {
	auto *insnd = rvtt_get_insn_data (*gsi);
	if (!insnd)
	  continue;

	if (!insnd->has_iptr ())
	  continue;

	auto *call = as_a <gcall *> (*gsi);
	tree immarg = gimple_call_arg (call, insnd->imm_arg ());
	const char *msg = nullptr;
	if (TREE_CODE (immarg) == INTEGER_CST)
	  {
	    if (integer_zerop (gimple_call_arg (call, 0)))
	      continue;
	    msg = "Constant";
	    gimple_call_set_arg (call, 0, null_pointer_node);
	    if (insnd->has_var ())
	      gimple_call_set_arg (call, insnd->var_arg (), integer_zero_node);
	  }
	else if (!insnd->has_var ())
	  msg = "Variable";
	else if (!integer_zerop (gimple_call_arg (call, insnd->var_arg ())))
	  msg = "User set";
	else
	  {
	    tree synth_val = build_int_cst (unsigned_type_node, synth_id++);
	    tree sum = build_var_synth (gsi, insnd, gimple_location (call),
					immarg, synth_val);

	    gimple_call_set_arg (call, insnd->var_arg (), sum);
	    gimple_call_set_arg (call, insnd->id_arg (), synth_val);
	    msg = "Synthesize";
	  }
	update_stmt (call);
	if (dump_file)
	  {
	    fprintf (dump_file, "%s:", msg);
	    print_gimple_stmt (dump_file, call, 0);
	  }
	updated = true;
      }

  return updated ? TODO_update_ssa : 0;
}

// After duplication optimizations we can end up with three cases of var_arg
// inputs that we should simplify:
//
// 1.  A phi where every add is of the same SSA var and that other op is from
// an identically numbered synth_opcode. Replace the phi with a newly
// synthesized synth_opcode and add pair.
//
// 2.  An add where the synth_operand input is a phi, and every incoming edge
// of that phi is an identically numbered synt.  Replace the phi with a newl
// synthesized synth_opcode.
//
// 3. A combination of the above two cases.

static unsigned
check_synth (std::unordered_set<gphi *> &phis, tree var, tree &id)
{
  if (!SSA_VAR_P (var))
    return false;

  gimple *stmt = SSA_NAME_DEF_STMT (var);
  if (auto *phi = dyn_cast<gphi *> (stmt))
    {
      if (!phis.insert (phi).second)
	return 0;

      unsigned count = 0;
      use_operand_p arg_p;
      ssa_op_iter ix;
      FOR_EACH_PHI_ARG (arg_p, phi, ix, SSA_OP_USE)
	{
	  unsigned inner = check_synth (phis, USE_FROM_PTR (arg_p), id);
	  if (!inner)
	    return 0;
	  count += inner;
	}
      return count;
    }

  auto *insnd = rvtt_get_insn_data (stmt);
  if (!insnd)
    return 0;

  if (insnd->id != rvtt_insn_data::synth_opcode)
    return 0;

  auto *call = as_a <gcall *> (stmt);
  tree id_arg = gimple_call_arg (call, 0);
  if (!id)
    id = id_arg;
  else if (!tree_int_cst_equal (id, id_arg))
    return 0;

  return 1;
}

static std::pair<unsigned, unsigned>
check_add (std::unordered_set<gphi *> &add_phis, std::unordered_set<gphi *> &synth_phis, tree var,
	   tree &id, tree &addend)
{
  if (!SSA_VAR_P (var))
    return {0, 0};

  gimple *stmt = SSA_NAME_DEF_STMT (var);
  if (auto *phi = dyn_cast<gphi *> (stmt))
    {
      if (!add_phis.insert (phi).second)
	return {0, 0};

      use_operand_p arg_p;
      ssa_op_iter ix;
      std::pair count {0, 0};
      FOR_EACH_PHI_ARG (arg_p, phi, ix, SSA_OP_USE)
	{
	  auto inner = check_add (add_phis, synth_phis, USE_FROM_PTR (arg_p), id, addend);
	  if (!inner.first)
	    return {0, 0};

	  count.first += inner.first;
	  count.second += inner.second;
	}
      return count;
    }

  if (auto *assign = dyn_cast<gassign *> (stmt))
    {
      if (gimple_assign_rhs_code (assign) != PLUS_EXPR)
	return {0, 0};

      tree first = gimple_assign_rhs1 (assign);
      tree other = gimple_assign_rhs2 (assign);

      // We don't know which rhs op is going to be the opcode -- it can vary
      synth_phis.clear ();
      unsigned synth_count = check_synth (synth_phis, first, id);
      if (!synth_count)
	{
	  synth_phis.clear ();
	  synth_count = check_synth (synth_phis, other, id);
	  if (!synth_count)
	    return {0, 0};
	  other = first;
	}
      if (!addend)
	addend = other;
      else if (other != addend)
	return {0, 0};

      return {1, synth_count};
    }

  return {0, 0};
}

static bool
cse_add (std::unordered_set<gphi *> &add_phis, std::unordered_set<gphi *> &synth_phis, tree var)
{
  tree id = nullptr;
  tree addend = nullptr;

  auto counts = check_add (add_phis, synth_phis, var, id, addend);
  if (!counts.first)
    return false;

  if (counts.first == 1 && counts.second == 1)
    return false;

  gimple *stmt = SSA_NAME_DEF_STMT (var);
  gimple_stmt_iterator gsi = gsi_for_stmt (stmt);
  gimple_stmt_iterator phi_gsi;
  gimple *opc_stmt = nullptr;

  // Synth a new synth_opcode
  if (counts.first == 1)
    {
      // We'll keep the add, add the synth just before it
      auto *assign = as_a <gassign *> (stmt);
      tree opc_var = gimple_assign_rhs1 (assign);
      if (opc_var == addend)
	opc_var = gimple_assign_rhs2 (assign);
      phi_gsi = gsi_for_stmt (SSA_NAME_DEF_STMT (opc_var));
      gsi = gsi_for_stmt (stmt);
      opc_stmt = make_synth (gimple_location (stmt), id, opc_var);
      gsi_insert_before (&gsi, opc_stmt, GSI_SAME_STMT);
    }
  else
    {
      // We'll create a new add, insert just after addend's def
      phi_gsi = gsi;
      auto *addend_def = SSA_NAME_DEF_STMT (addend);
      gsi = is_a <gphi *> (addend_def)
	? gsi_after_labels (gimple_bb (addend_def))
	: gsi_for_stmt (addend_def);
      opc_stmt = make_synth (gimple_location (stmt), id, nullptr);
      gsi_insert_after (&gsi, opc_stmt, GSI_NEW_STMT);
    }

  if (dump_file)
    {
      fprintf (dump_file, "Replacing %u synths with single ", counts.second);
      print_gimple_stmt (dump_file, opc_stmt, 0);
    }

  if (counts.first > 1)
    {
      // Synth new add
      tree sum_var = gimple_phi_result (as_a <gphi *> (stmt));
      tree opc_var = gimple_call_lhs (opc_stmt);
      auto *sum_stmt = gimple_build_assign (sum_var, PLUS_EXPR, opc_var, addend);
      gsi_insert_after (&gsi, sum_stmt, GSI_SAME_STMT);

      if (dump_file)
	{
	  fprintf (dump_file, "Replacing %u adds with single ", counts.first);
	  print_gimple_stmt (dump_file, sum_stmt, 0);
	}
    }

  remove_phi_node (&phi_gsi, false);

  return true;
}

static unsigned
cse (function *fn)
{
  std::unordered_set<tree> handled;
  std::unordered_set<gphi *> add_phis;
  std::unordered_set<gphi *> synth_phis;

  basic_block bb;

  bool updated = false;
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	 !gsi_end_p (gsi); gsi_next (&gsi))
      {
	auto *insnd = rvtt_get_insn_data (*gsi);
	if (!insnd)
	  continue;
	if (!insnd->has_var ())
	  continue;
	auto *call = as_a <gcall *> (*gsi);
	tree var = gimple_call_arg (call, insnd->var_arg ());
	if (!SSA_VAR_P (var))
	  continue;
	if (!handled.insert (var).second)
	  continue;

	add_phis.clear ();
	if (cse_add (add_phis, synth_phis, var))
	  updated = true;
      }

  return updated ? TODO_update_ssa : 0;
}

// After duplicative optimizations like loop unrolling and
// loop unswitching we can have
//
// a) Uses with a now-constant argument. Delete the synth
//
// b) Multiple synth_opcodes with the same ID. This is when CSE decides not to
// eliminate. We should renumber these to give the backend more flexibility
//   a = opcode_synth
//   b = add (a, val)
//   USE b
//   c = add (a, val)
//   USE c
//
// c) Multiple opcode adds using the same synth_opcode.  This happens is when
// there are different non-immediate input values to the duplicated use. We want
// to insert new synth_opcodes so there's a 1:1 mapping to the adds.
//
// Complicating things is induction variable deduction.  We can get a sequences
// of:
//   a = opcode_synth
//   b = add (a, CONST)
//   c = add (b, val)
//   USE c
//   d = add (c, CONST)
//   USE d
//   ...
//
// we should renumber the separate final uses, which involves duplicatiing the
// IV add.
//
// Finally we could get chains that endup in a PHI.  At that point we need to
// preserve that particular path -- but can renumber uses before the PHI.

namespace {
struct synth;

// Root nodes (all synth_opcodes)
using roots_t = std::unordered_map<unsigned, synth *>;
#if ENABLE_ASSERT_CHECKING
// All nodes (to detect merges -- completely unexpected)
using nodes_t = std::unordered_map<gimple *, synth *>;
#else
struct nodes_t {};
#endif

struct synth
{
  synth *sib = nullptr; // sibling use of the same parent (or of same synth ID)
  synth *child = nullptr; // use(s) of this insn
  gimple *stmt = nullptr; // the stmt, NULL if cannot continue
  bool other_is_rhs2 = false; // which arg of stmt is the other value
  unsigned short use_flags = 0;

  enum USE {
    USE_IsFinal = 0x01,
    USE_IsUnfollowable = 0x02,
    USE_IsAdd = 0x04,

    USE_ChildFinal = 0x10,
    USE_ChildAdd = 0x20,
    USE_ChildMask = 0xf0,

    USE_HasFinal = 0x100,
    USE_HasUnfollowable = 0x200,
    USE_HasAdds = 0x400,
    USE_HasManyAdds = 0x800,
  };

  static constexpr auto SPACES = "               ";

  synth (gimple *stmt)
    : stmt (stmt) {}

  unsigned build (nodes_t &nodes, unsigned id, unsigned depth = 0, bool seen_var_add = false)
  {
    imm_use_iterator iter;
    use_operand_p use_p;
    auto ssa_var = gimple_get_lhs (stmt);
    FOR_EACH_IMM_USE_FAST (use_p, iter, ssa_var)
      {
	gimple *use_stmt = USE_STMT (use_p);

	if (is_gimple_debug (use_stmt))
	  continue;

	// Allocate and chain a child node
	auto *use_node = new (ggc_alloc<synth> ()) synth (use_stmt);
	use_node->sib = child;
	child = use_node;

	{
#if ENABLE_ASSERT_CHECKING
	  auto [seen, not_seen] = nodes.emplace (use_stmt, use_node);
	  gcc_assert (not_seen);
#endif
	}

	if (rvtt_get_insn_data (use_stmt))
	  {
	    // A final use
	    if (dump_file)
	      {
		fprintf (dump_file, "%u:%.*sFinal use ", id, depth * 2, SPACES);
		print_gimple_stmt (dump_file, use_stmt, 0);
	      }
	    use_node->use_flags |= USE_IsFinal;
	    use_flags |= USE_ChildFinal | USE_HasFinal;
	    continue;
	  }

	auto *add_stmt = dyn_cast <gassign *> (use_stmt);
	if (!add_stmt || gimple_assign_rhs_code (add_stmt) != PLUS_EXPR)
	  {
	  unfollowable:
	    if (dump_file)
	      {
		fprintf (dump_file, "%u:%.*sUnfollowable ", id, depth * 2, SPACES);
		print_gimple_stmt (dump_file, use_stmt, 0);
	      }
	    use_node->use_flags = USE_IsUnfollowable;
	    use_flags |= USE_HasUnfollowable;
	    continue;
	  }

	tree other_arg = gimple_assign_rhs1 (add_stmt);
	if (other_arg == ssa_var)
	  {
	    other_arg = gimple_assign_rhs2 (add_stmt);
	    use_node->other_is_rhs2 = true;
	  }
	else
	  gcc_assert (gimple_assign_rhs2 (add_stmt) == ssa_var);

	bool now_seen_var_add = seen_var_add;
	if (TREE_CODE (other_arg) != INTEGER_CST)
	  {
	    if (seen_var_add)
	      goto unfollowable;
	    now_seen_var_add = true;
	  }

	if (dump_file)
	  {
	    fprintf (dump_file, "%u:%.*sFollowing ", id, depth * 2, SPACES);
	    print_gimple_stmt (dump_file, use_stmt, 0);
	  }

	use_flags |= use_node->build (nodes, id, depth + 1, now_seen_var_add);

	use_node->use_flags |= USE_IsAdd;
	if (use_flags & USE_ChildAdd)
	  use_flags |= USE_HasManyAdds;
	if (use_node->use_flags & USE_HasManyAdds)
	  use_flags |= USE_HasManyAdds;
	use_flags |= USE_ChildAdd;
      }

    return use_flags & ~USE_ChildMask;
  }

  // Renumber the chain beginning here.
  void renumber (unsigned id, unsigned &id_hwm, unsigned depth = 0,
		 uint32_t addend = 0, synth const *val_node = nullptr) const
  {
    if (sib)
      sib->renumber (id, id_hwm, depth, addend, val_node);

    if (use_flags & (USE_IsFinal | USE_IsUnfollowable))
      return;

    // Figure addend and val_node
    if (use_flags & USE_IsAdd)
      {
	auto *add_stmt = dyn_cast <gassign *> (stmt);
	tree other = other_is_rhs2 ? gimple_assign_rhs2 (add_stmt) : gimple_assign_rhs1 (add_stmt);
	if (TREE_CODE (other) == INTEGER_CST)
	  addend += TREE_INT_CST_LOW (other);
	else
	  val_node = this;
      }
    else
      {
	gcc_assert (rvtt_get_insn_data (stmt)->id == rvtt_insn_data::synth_opcode);
	addend = TREE_INT_CST_LOW (gimple_call_arg (stmt, 1));
      }

    if (use_flags & USE_ChildFinal)
      {
	// new synth and (probably) val_node
	tree ssa_var = make_temp_ssa_name (unsigned_type_node, NULL, "li");
	tree new_id = build_int_cst (unsigned_type_node, id_hwm++);
	tree synth_opcode_decl = rvtt_get_insn_data (rvtt_insn_data::synth_opcode)->decl;
	gcall *new_call = gimple_build_call (synth_opcode_decl, 2);
	gimple_call_set_arg (new_call, 0, new_id);
	gimple_call_set_arg (new_call, 1, build_int_cst (unsigned_type_node, addend));
	gimple_call_set_lhs (new_call, ssa_var);
	gimple_set_location (new_call, gimple_location (stmt));
	auto gsi = gsi_for_stmt (stmt);
	gsi_insert_after (&gsi, new_call, GSI_NEW_STMT);
	if (dump_file)
	  {
	    fprintf (dump_file, "%u:%.*sCreating new ", id, depth * 2, SPACES);
	    print_gimple_stmt (dump_file, new_call, 0);
	  }

	if (val_node)
	  {
	    // Duplicate val addition
	    tree add_res = make_temp_ssa_name (unsigned_type_node, NULL, "val");
	    tree val_arg = val_node->other_is_rhs2
	      ? gimple_assign_rhs2 (val_node->stmt) : gimple_assign_rhs1 (val_node->stmt);
	    gassign *new_add = gimple_build_assign (add_res, PLUS_EXPR, ssa_var, val_arg);
	    gimple_set_location (new_add, gimple_location (stmt));
	    gsi_insert_after (&gsi, new_add, GSI_NEW_STMT);
	    if (dump_file)
	      {
		fprintf (dump_file, "%u:%.*sCloning add ", id, depth * 2, SPACES);
		print_gimple_stmt (dump_file, new_add, 0);
	      }
	    ssa_var = add_res;
	  }

	for (auto *final = child; final; final = final->sib)
	  if (final->use_flags & USE_IsFinal)
	    {
	      auto *insnd = rvtt_get_insn_data (final->stmt);
	      gimple_call_set_arg (final->stmt, insnd->var_arg (), ssa_var);
	      gimple_call_set_arg (final->stmt, insnd->id_arg (), new_id);
	      update_stmt (final->stmt);
	      if (dump_file)
		{
		  fprintf (dump_file, "%u:%.*sUpdating use ", id, depth * 2, SPACES);
		  print_gimple_stmt (dump_file, final->stmt, 0);
		}
	    }
      }

    if (use_flags & USE_HasFinal)
      child->renumber (id, id_hwm, depth + 1, addend, val_node);
  }

  // Delete insns that are no longer needed
  void cleanup (unsigned id, unsigned depth = 0) const
  {
    if (sib)
      sib->cleanup (id, depth);

    if (child)
      child->cleanup (id, depth + 1);

    if (use_flags & (USE_IsFinal | USE_IsUnfollowable))
      return;

    if (has_zero_uses (gimple_get_lhs (stmt)))
      {
	if (dump_file)
	  {
	    fprintf (dump_file, "%u:%.*sDeleting ", id, depth * 2, SPACES);
	    print_gimple_stmt (dump_file, stmt, 0);
	  }
	auto gsi = gsi_for_stmt (stmt);
	unlink_stmt_vdef (stmt);
	gsi_remove (&gsi, true);
      }
  }
};
}

// Renumber synth_opcode ids.  Gather all the synth_opcodes and the subsequent
// use chain.  Then renumber those that would benefit.

static unsigned
renumber (function *fn)
{
  roots_t roots;
  nodes_t nodes;
  unsigned id_hwm = 0;

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (auto gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
      if (auto *call_stmt = dyn_cast <gcall *> (gsi_stmt (gsi)))
	if (auto *insnd = rvtt_get_insn_data (call_stmt))
	  if (insnd->id == rvtt_insn_data::synth_opcode)
	    {
	      unsigned id = TREE_INT_CST_LOW (gimple_call_arg (call_stmt, 0));

	      if (id >= id_hwm)
		id_hwm = id + 1;
	      synth *node = new (ggc_alloc<synth> ()) synth (call_stmt);
	      auto [IX, inserted] = roots.emplace (id, node);
	      if (!inserted)
		{
		  // Chain on
		  if (dump_file)
		    fprintf (dump_file, "%u: Multiple synth_opcodes\n", id);
		  node->sib = IX->second;
		  IX->second = node;
		}
	      node->build (nodes, id);
	    }

  bool renumbered = false;
  for (auto [id, root] : roots)
    {
      if (!root->sib
	  && ((root->use_flags & ~synth::USE_ChildMask) == synth::USE_HasFinal
	      || (root->use_flags & ~synth::USE_ChildMask) == synth::USE_HasUnfollowable))
	{
	  if (dump_file)
	    fprintf (dump_file, "%u: Not renumbering\n", id);
	  continue;
	}

      root->renumber (id, id_hwm);
      root->cleanup (id);
      renumbered = true;
    }

  return renumbered ? TODO_update_ssa : 0;
}

namespace {

const pass_data pass_data_rvtt_synth_split =
{
  GIMPLE_PASS, /* type */
  "rvtt_synth_split", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_synth_split : public gimple_opt_pass
{
public:
  pass_rvtt_synth_split (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_synth_split, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }

  virtual unsigned execute (function *fn) override
  {
    return split (fn);
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_synth_split (gcc::context *ctxt)
{
  return new pass_rvtt_synth_split (ctxt);
}

namespace {

const pass_data pass_data_rvtt_synth_cse =
{
  GIMPLE_PASS, /* type */
  "rvtt_synth_cse", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_synth_cse : public gimple_opt_pass
{
public:
  pass_rvtt_synth_cse (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_synth_cse, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }

  virtual unsigned execute (function *fn) override
  {
    return cse (fn);
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_synth_cse (gcc::context *ctxt)
{
  return new pass_rvtt_synth_cse (ctxt);
}

namespace {

const pass_data pass_data_rvtt_synth_renumber =
{
  GIMPLE_PASS, /* type */
  "rvtt_synth_renumber", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa | PROP_cfg, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_synth_renumber : public gimple_opt_pass
{
public:
  pass_rvtt_synth_renumber (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_synth_renumber, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }

  virtual unsigned execute (function *fn) override
  {
    return renumber (fn);
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_synth_renumber (gcc::context *ctxt)
{
  return new pass_rvtt_synth_renumber (ctxt);
}
