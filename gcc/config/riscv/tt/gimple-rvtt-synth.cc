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
#include "tree-into-ssa.h"
#include "rvtt.h"
#include <unordered_set>

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
  gimple_call_set_arg (stmt, 0, integer_zero_node);
  gimple_call_set_arg (stmt, 1, id);
  gimple_call_set_lhs (stmt, var);
  gimple_set_location (stmt, loc);

  return stmt;
}

static tree
build_var_synth (gimple_stmt_iterator gsi, const rvtt_insn_data *insnd,
		 location_t loc, tree val, tree id)
{
  uint32_t mask =  (uint32_t(1) << insnd->imm_bits ()) - 1;
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
  unsigned int synth_id = 0;
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
	tree immarg = gimple_call_arg (call, insnd->imm_arg ());
	const char *msg = nullptr;
	if (TREE_CODE (immarg) == INTEGER_CST)
	  {
	    msg = "Constant";
	    gimple_call_set_arg (call, 0, null_pointer_node);
	    gimple_call_set_arg (call, insnd->var_arg (), integer_zero_node);
	  }
	else if (!integer_zerop (gimple_call_arg (call, insnd->var_arg ())))
	  msg = "User set";
	else
	  {
	    synth_id += 2; // We may need to insert another one later
	    tree synth_val = build_int_cst (unsigned_type_node, synth_id);
	    tree sum = build_var_synth (gsi, insnd, gimple_location (call),
					immarg, synth_val);

	    gimple_call_set_arg (call, insnd->var_arg (), sum);
	    gimple_call_set_arg (call, insnd->id_arg (), synth_val);
	    msg = "Variable";
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
  tree id_arg = gimple_call_arg (call, 1);
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
  tree opc_var = nullptr;
  if (counts.first == 1)
    {
      auto *assign = as_a <gassign *> (stmt);
      opc_var = gimple_assign_rhs1 (assign);
      if (opc_var == addend)
	opc_var = gimple_assign_rhs2 (assign);
      phi_gsi = gsi_for_stmt (SSA_NAME_DEF_STMT (opc_var));
      gsi = gsi_for_stmt (stmt);
    }
  else
    {
      phi_gsi = gsi;
      gsi = gsi_after_labels (gimple_bb (stmt));
    }

  // Synth a new synth_opcode
  auto *opc_stmt = make_synth (gimple_location (stmt), id, opc_var);
  gsi_insert_before (&gsi, opc_stmt, GSI_SAME_STMT);
  if (dump_file)
    {
      fprintf (dump_file, "Replacing %u synths with single ", counts.second);
      print_gimple_stmt (dump_file, opc_stmt, 0);
    }

  if (counts.first > 1)
    {
      tree sum_var = gimple_phi_result (as_a <gphi *> (stmt));
      opc_var = gimple_call_lhs (opc_stmt);
      auto *sum_stmt = gimple_build_assign (sum_var, PLUS_EXPR, opc_var, addend);
      gsi_insert_before (&gsi, sum_stmt, GSI_SAME_STMT);

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
// 0 uses with a now-constant argument
// 1 multiple synth_opcodes with the same ID
// 2 multiple opcode adds using the same synth_opcode
// #0 we should delete
// #1 is when CSE decides not to eliminate. We may as well renumber
// these to give the backend more flexibility (we presume it's
// unlikely to do CSE we didn't).
// #2 is when there are different non-immediate input values to the
// dupliated use. We want to insert new synth_opcodes so there's a 1:1
// mapping to the adds.
// Applying the renumbering and insertion requires following SSA
// DEF/USE lists. If we encounter something odd (like a PHI), we
// abandon renumbering that ID.

static unsigned
renumber (function *fn)
{
  struct node_t
  {
    gimple *stmt;
    unsigned opcode_ix;
    unsigned add_ix;
    bool rhs2 : 1; // add: incoming edge is rhs2
    bool used : 1;
    bool phi_use : 1;
    unsigned id : 30;
    unsigned addend; // opcode: count of uses
		     // use stmt: nonimm pos

    static node_t opcode (gimple *stmt, unsigned ix, unsigned id)
    {
      return {stmt, ix, 0, false, false, false, id, 0};
    }
    static node_t add (gimple *stmt, unsigned op_ix, unsigned ix, bool rhs2, unsigned addend)
    {
      return {stmt, op_ix, ix, rhs2, false, false, 0, addend};
    }
    static node_t use (gimple *stmt, unsigned op_ix, unsigned add_ix, unsigned nonimm)
    {
      return {stmt, op_ix, add_ix, false, false, false, 0, nonimm};
    }
  };

  auto build_graph = [] (auto &self, std::vector<node_t> &graph, unsigned opcode_ix, tree ssa_var,
			  unsigned first_add_ix = 0, unsigned last_add_ix = 0, unsigned addend = 0)
    -> unsigned
  {
    bool any_nonimm = false;
    unsigned add_count = 0;

    imm_use_iterator iter;
    use_operand_p use_p;
    FOR_EACH_IMM_USE_FAST (use_p, iter, ssa_var)
      {
	gimple *use_stmt = USE_STMT (use_p);
	if (is_gimple_call (use_stmt))
	  {
	    // a final use
	    auto *insnd = rvtt_get_insn_data (use_stmt);
	    if (dump_file)
	      {
		fprintf (dump_file, "%u:Final use of %u via %u ",
			 unsigned (graph.size ()), opcode_ix, last_add_ix);
		print_gimple_stmt (dump_file, use_stmt, 0);
	      }
	    graph.emplace_back (node_t::use (use_stmt, opcode_ix,
					     last_add_ix, insnd->imm_arg ()));
	    any_nonimm = true;
	  }
	else if (auto *add_stmt = dyn_cast <gassign *> (use_stmt))
	  {
	    // an add
	    gcc_assert (gimple_assign_rhs_code (add_stmt) == PLUS_EXPR);
	    tree opcode_arg = gimple_assign_rhs1 (add_stmt);
	    bool is_op2 = opcode_arg != ssa_var;
	    gcc_assert (!is_op2
			|| gimple_assign_rhs2 (add_stmt) == ssa_var);

	    unsigned this_add_ix = graph.size ();
	    unsigned this_first_add_ix = this_add_ix;
	    unsigned self_add_ix = first_add_ix;
	    unsigned this_addend = addend;
	    tree other = is_op2 ? opcode_arg : gimple_assign_rhs2 (add_stmt);
	    if (TREE_CODE (other) == INTEGER_CST)
	      {
		gcc_assert (first_add_ix || !this_addend);
		this_addend += TREE_INT_CST_LOW (other);
		this_first_add_ix = first_add_ix;
	      }
	    else
	      {
		gcc_assert (!first_add_ix);
		if (this_addend)
		  self_add_ix = this_add_ix;
	      }
	    if (dump_file)
	      {
		fprintf (dump_file, "%u:Add of %u, via %u ",
			 unsigned (graph.size ()), opcode_ix, self_add_ix);
		print_gimple_stmt (dump_file, add_stmt, 0);
	      }
	    graph.emplace_back (node_t::add (add_stmt, opcode_ix, self_add_ix,
					     is_op2, this_addend));

	    bool is_used = self (self, graph, opcode_ix, gimple_get_lhs (add_stmt),
				 this_first_add_ix, this_add_ix, this_addend);
	    gcc_assert (is_used);
	    if (this_first_add_ix)
	      graph[this_add_ix].used = 1;
	    add_count += 1;
	  }
	else if (auto *phi_stmt = dyn_cast <gphi *> (use_stmt))
	  {
	    // We can encounter this when code duplication has cloned
	    // synth_opcode insns in various blocks. We should be able
	    // to CSE these cases into a dominator. Unfortunately
	    // there doesn't appear to be a (reusable) gimple CSE
	    // pass. For now mark this id as not to be touched :(
	    if (first_add_ix)
	      {
		// The PHI is merging adds of synth_opcode values
		gcc_assert (first_add_ix == last_add_ix);
		graph[first_add_ix].phi_use = true;
		add_count++;
	      }
	    else
	      {
		// The PHI is merging synth_opcode values themselves. Stop
		// following, and then we'll not touch qnything here.
	      }
	    if (dump_file)
	      {
		fprintf (dump_file, "Phi use of %s %u ", first_add_ix ? "add" : "opcode",
			 first_add_ix ? first_add_ix : opcode_ix);
		print_gimple_stmt (dump_file, phi_stmt, 0);
	      }
	  }
	else
	  gcc_assert (gimple_code (use_stmt) == GIMPLE_DEBUG);
      }
    return add_count + unsigned (any_nonimm);
  };

  // Build the opcode-use graph
  std::vector<node_t> graph;
  std::vector<unsigned> opcode_counts;

  if (dump_file)
    fprintf (dump_file, "Building graph\n");
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (auto gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
      if (auto *call_stmt = dyn_cast <gcall *> (gsi_stmt (gsi)))
	{
	  auto *insnd = rvtt_get_insn_data (call_stmt);
	  if (!insnd)
	    continue;
	  if (insnd->id == rvtt_insn_data::synth_opcode)
	    {
	      unsigned id = TREE_INT_CST_LOW (gimple_call_arg (call_stmt, 1));;
	      if (opcode_counts.size() < id + 1)
		opcode_counts.resize (id + 1);
	      opcode_counts[id]++;
	      unsigned opcode_ix = graph.size ();
	      graph.emplace_back (node_t::opcode (call_stmt, opcode_ix, id));
	      unsigned count = build_graph (build_graph, graph, opcode_ix, gimple_call_lhs (call_stmt));
	      graph[opcode_ix].addend = count;
	    }
	}

  if (dump_file)
    fprintf (dump_file, "\nProcessing graph\n");

  bool renumbered = false;
  unsigned unique_id = opcode_counts.size () - 1;
  for (auto &node : graph)
    {
      if (!is_gimple_assign (node.stmt))
	continue;

      gcc_assert (node.used);
      if (!node.add_ix && !node.addend)
	{
	  if (node.phi_use)
	    // Has a phi_use, do not alter this add
	    continue;

	  auto &opcode_slot = graph[node.opcode_ix];
	  if (!--opcode_slot.addend)
	    {
	      auto &count_slot = opcode_counts[opcode_slot.id];
	      if (!--count_slot)
		continue;
	    }
	}

      // A synth opcode used by more than one add. Insert a renumbered
      // synth opcode.
      unique_id += 2;
      node.id = unique_id;

      tree addend = integer_zero_node;
      if (node.addend)
	{
	  // Propagate the iv_var
	  gcc_assert (node.add_ix);
	  auto &add_slot = graph[node.add_ix];
	  auto iv_var = add_slot.rhs2
	    ? gimple_assign_rhs1 (add_slot.stmt)
	    : gimple_assign_rhs2 (add_slot.stmt);
	  if (node.rhs2)
	    gimple_assign_set_rhs1 (node.stmt, iv_var);
	  else
	    gimple_assign_set_rhs2 (node.stmt, iv_var);
	  addend = build_int_cst (unsigned_type_node, node.addend);
	}

      // Create a new synth opcode
      tree ssa_var = make_temp_ssa_name (unsigned_type_node, NULL, "li");
      tree synth_opcode_decl = rvtt_get_insn_data (rvtt_insn_data::synth_opcode)->decl;
      gcall *new_call = gimple_build_call (synth_opcode_decl, 2);
      gimple_call_set_arg (new_call, 0, addend);
      gimple_call_set_arg (new_call, 1, build_int_cst (unsigned_type_node, unique_id));
      gimple_call_set_lhs (new_call, ssa_var);

      // Insert it just before the add, and update the add's operand
      gimple_set_location (new_call, gimple_location (graph[node.opcode_ix].stmt));
      auto gsi = gsi_for_stmt (node.stmt);
      gsi_insert_before (&gsi, new_call, GSI_NEW_STMT);
      if (node.rhs2)
	gimple_assign_set_rhs2 (node.stmt, ssa_var);
      else
	gimple_assign_set_rhs1 (node.stmt, ssa_var);
      update_stmt (node.stmt);

      if (dump_file)
	{
	  fprintf (dump_file, "Creating new synth ");
	  print_gimple_stmt (dump_file, new_call, 0);
	  fprintf (dump_file, "Updating add ");
	  print_gimple_stmt (dump_file, node.stmt, 0);
	}

      renumbered = true;
    }

  if (renumbered)
    {
      // Update all the uses of the renumbered adds
      if (dump_file)
	fprintf (dump_file, "\nRenumbering\n");
      for (auto &slot : graph)
	{
	  if (!is_gimple_call (slot.stmt))
	    continue;
	  if (!slot.add_ix)
	    continue;

	  // A use, see if it needs renumbering
	  if (unsigned id = graph[slot.add_ix].id)
	    {
	      gimple_call_set_arg (slot.stmt, slot.addend + 2,
				   build_int_cst (unsigned_type_node, id));
	      update_stmt (slot.stmt);
	      if (dump_file)
		print_gimple_stmt (dump_file, slot.stmt, 2);
	    }
	}
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
