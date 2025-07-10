/* Pass to renumber synth_opcode ids of duplicate code sequences
   Copyright (C) 2025 Tenstorrent Inc.
   Nathan Sidwell (nsidwell@tenstorrent.com).

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
#include "tree-into-ssa.h"
#include "rvtt.h"
#include <unordered_map>

#include "gimple-pretty-print.h"

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
transform (function *fn)
{
  struct node_t
  {
    gimple *stmt;
    unsigned opcode_ix;
    unsigned add_ix;
    bool rhs2 : 1;
    bool used : 1;
    unsigned id : 30;
    unsigned addend; // opcode count of uses
                     // use stmt: nonimm pos

    static node_t opcode (gimple *stmt, unsigned ix, unsigned id)
    {
      return {stmt, ix, 0, false, false, id, 0};
    }
    static node_t add (gimple *stmt, unsigned op_ix, unsigned ix, bool rhs2, unsigned addend)
    {
      return {stmt, op_ix, ix, rhs2, false, 0, addend};
    }
    static node_t use (gimple *stmt, unsigned op_ix, unsigned add_ix, unsigned nonimm)
    {
      return {stmt, op_ix, add_ix, false, false, 0, nonimm};
    }
  };
  std::vector<gcall *> opcode_stmts;
  std::vector<unsigned> opcode_counts;
  std::vector<node_t> graph;

  int updated = false; // true/false/file-not-found :)

  // Find all the synth_opcodes and nonimm uses
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (auto gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
      {
	gcall *stmt;
	const rvtt_insn_data *insnd;

	if (!rvtt_p (&insnd, &stmt, gsi))
	  continue;
	if (insnd->id == rvtt_insn_data::synth_opcode)
	  {
	    opcode_stmts.push_back (stmt);
	    continue;
	  }

	if (insnd->nonimm_pos < 0)
	  continue;
	if (TREE_CODE (gimple_call_arg (stmt, insnd->nonimm_pos)) == INTEGER_CST)
	  {
	    // It's now a constant, zap the synth arg and pointer
	    gimple_call_set_arg (stmt, 0, null_pointer_node);
	    gimple_call_set_arg (stmt, insnd->nonimm_pos + 1, integer_zero_node);
	    gimple_call_set_arg (stmt, insnd->nonimm_pos + 2, integer_zero_node);
	    update_stmt (stmt);
	    updated = true;
	    continue;
	  }
      }

  if (opcode_stmts.empty ())
    return updated ? TODO_update_ssa : 0;

  auto build_graph = [&] (auto &self, unsigned opcode_ix, tree ssa_var, unsigned add_ix = 0, unsigned addend = 0)
    -> unsigned
  {
    imm_use_iterator iter;
    gimple *use_stmt;
    bool any = false;
    unsigned count = 0;

    // FIXME: Can we use quick iterator here?
    FOR_EACH_IMM_USE_STMT (use_stmt, iter, ssa_var)
      {
	if (is_gimple_call (use_stmt))
	  {
	    // a final use
	    gcall *stmt;
	    const rvtt_insn_data *insnd;

	    bool is_rvtt = rvtt_p (&insnd, &stmt, use_stmt);
	    gcc_assert (is_rvtt && insnd->nonimm_pos >= 0);
	    if (gimple_call_arg (stmt, insnd->nonimm_pos + 1)
		!= integer_zero_node)
	      {
		graph.emplace_back (node_t::use (use_stmt, opcode_ix,
						 add_ix, insnd->nonimm_pos));
		any = true;
	      }
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
	    unsigned first_add_ix = this_add_ix;
	    if (add_ix)
	      {
		tree offset = is_op2 ? opcode_arg : gimple_assign_rhs2 (add_stmt);
		if (TREE_CODE (offset) != INTEGER_CST)
		  {
		    debug_gimple_stmt (graph[opcode_ix].stmt);
		    debug_gimple_stmt (graph[add_ix].stmt);
		    debug_gimple_stmt (use_stmt);
		    gcc_assert (TREE_CODE (offset) == INTEGER_CST);
		  }
		addend += TREE_INT_CST_LOW (offset);
		first_add_ix = add_ix;
	      }
	    graph.emplace_back (node_t::add (add_stmt, opcode_ix, add_ix, is_op2, addend));

	    bool used = self (self, opcode_ix, gimple_get_lhs (add_stmt), first_add_ix, addend);
	    graph[this_add_ix].used = used;
	    count += used;
	  }
	else if (gimple_code (use_stmt) != GIMPLE_DEBUG)
	  {
	    debug_gimple_stmt (graph[opcode_ix].stmt);
	    if (add_ix)
	      debug_gimple_stmt (graph[add_ix].stmt);
	    debug_gimple_stmt (use_stmt);
	    gcc_assert (false);
	  }
      }
    return count + unsigned (any);
  };

  tree synth_opcode_decl = rvtt_get_insn_data (rvtt_insn_data::synth_opcode)->decl;
  for (gimple *opcode_stmt : opcode_stmts)
    {
      unsigned id = TREE_INT_CST_LOW (gimple_call_arg (opcode_stmt, 1));;
      if (opcode_counts.size() < id + 1)
	opcode_counts.resize (id + 1);
      opcode_counts[id]++;
      unsigned opcode_ix = graph.size ();
      graph.emplace_back (node_t::opcode (opcode_stmt, opcode_ix, id));
      unsigned count = build_graph (build_graph, opcode_ix, gimple_call_lhs (opcode_stmt));
      graph[opcode_ix].addend = count;
    }

  unsigned unique_id = opcode_counts.size () - 1;
  for (auto &node : graph)
    {
      if (!is_gimple_assign (node.stmt))
	continue;

      if (!node.used)
	continue;

      if (!node.add_ix)
	{
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
      if (node.add_ix)
	{
	  // Propagate the iv_var
	  auto &add_slot = graph[node.add_ix];
	  auto iv_var = add_slot.rhs2
	    ? gimple_assign_rhs1 (add_slot.stmt)
	    : gimple_assign_rhs1 (add_slot.stmt);
	  if (node.rhs2)
	    gimple_assign_set_rhs1 (node.stmt, iv_var);
	  else
	    gimple_assign_set_rhs2 (node.stmt, iv_var);
	  addend = build_int_cst (unsigned_type_node, node.addend);
	  
	}

      // Create a new synth opcode
      tree ssa_var = make_temp_ssa_name (unsigned_type_node, NULL, "li");
      gcall *new_call = gimple_build_call (synth_opcode_decl, 2);
      gimple_call_set_arg (new_call, 0, addend);
      gimple_call_set_arg (new_call, 1, build_int_cst (unsigned_type_node, unique_id));
      gimple_call_set_lhs (new_call, ssa_var);
      update_stmt (new_call);

      // Insert it just before the add, and update the add's operand
      gimple_set_location (new_call, gimple_location (graph[node.opcode_ix].stmt));
      auto gsi = gsi_for_stmt (node.stmt);
      gsi_insert_before (&gsi, new_call, GSI_NEW_STMT);
      if (node.rhs2)
	gimple_assign_set_rhs2 (node.stmt, ssa_var);
      else
	gimple_assign_set_rhs1 (node.stmt, ssa_var);
      update_stmt (node.stmt);

#if 0
      auto *chain = &node;
      auto add_stmt = node.stmt;
      for (;;)
	{
	  unsigned parent_ix = chain->add_ix;
	  tree ssa_var = make_temp_ssa_name (unsigned_type_node, NULL,
					     parent_ix ? "sum" : "li");
	  gimple *new_stmt = nullptr;
	  gimple *parent_stmt;
	  if (parent_ix)
	    {
	      parent_stmt = graph[parent_ix].stmt;
	      new_stmt = gimple_build_assign
		(ssa_var, PLUS_EXPR,
		 gimple_assign_rhs1 (parent_stmt),
		 gimple_assign_rhs2 (parent_stmt));
	    }
	  else
	    {
	      parent_stmt = graph[chain->opcode_ix].stmt;
	      gcall *new_call = gimple_build_call (synth_opcode_decl, 2);
	      gimple_call_set_arg (new_call, 0, integer_zero_node);
	      gimple_call_set_arg (new_call, 1, build_int_cst (unsigned_type_node, unique_id));
	      gimple_call_set_lhs (new_call, ssa_var);
	      update_stmt (new_call);
	      new_stmt = new_call;
	    }

	  gimple_set_location (new_stmt, gimple_location (parent_stmt));
	  auto parent_gsi = gsi_for_stmt (parent_stmt);
	  gsi_insert_after (&parent_gsi, new_stmt, GSI_NEW_STMT);

	  if (chain->rhs2)
	    gimple_assign_set_rhs2 (add_stmt, ssa_var);
	  else
	    gimple_assign_set_rhs1 (add_stmt, ssa_var);
	  update_stmt (add_stmt);

	  if (!parent_ix)
	    break;

	  add_stmt = new_stmt;
	  chain = &graph[parent_ix];
	}
#endif
      updated = -1;
    }

  if (updated < 0)
    // Update all the uses of the renumbered adds
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
	  }
      }

  return updated ? TODO_update_ssa : 0;
}

namespace {

const pass_data pass_data_rvtt_synth_renumber =
{
  GIMPLE_PASS, /* type */
  "rvtt_synth_renumber", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
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
    return TARGET_RVTT;
  }

  virtual unsigned execute (function *fn) override
  {
    return transform (fn);
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_synth_renumber (gcc::context *ctxt)
{
  return new pass_rvtt_synth_renumber (ctxt);
}
