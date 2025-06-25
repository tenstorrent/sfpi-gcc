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
  struct use_info
  {
    gimple *opcode_stmt = nullptr;
    gimple *add_stmt = nullptr;
    unsigned count = 0; // opcode, add: count of adds
                        // add: new-id
                        // use stmt: nonimm pos
    bool flag = false;  // add:rhs2
    bool used = false;

    use_info () = default;
    use_info (unsigned pos) : count (pos) {}
    use_info (gimple *op, gimple *add, bool f)
      : opcode_stmt (op), add_stmt (add), flag (f) {}
  };
  std::unordered_map<gimple *, use_info> map;
  std::vector<gimple *> synth_opcodes;

  int updated = false; // true/false/file-not-found :)
  unsigned unique_id = 0;

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
	    unsigned id = TREE_INT_CST_LOW (gimple_call_arg (stmt, 1));;
	    if (unique_id < id)
	      unique_id = id;
	    map.insert ({stmt, {}});
	    synth_opcodes.push_back (stmt);
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
	map.insert ({stmt, {insnd->nonimm_pos}});
      }

  if (map.empty ())
    return updated ? TODO_update_ssa : 0;

  auto build_graph = [&] (auto &self, gimple *opcode_stmt, tree ssa_var, gimple *add_stmt = nullptr)
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
	    auto slot = map.find (use_stmt);
	    if (slot == map.end())
	      {
		debug_gimple_stmt (opcode_stmt);
		debug_gimple_stmt (add_stmt);
		debug_gimple_stmt (use_stmt);
		gcc_assert (false);
	      }

	    slot->second.opcode_stmt = opcode_stmt;
	    slot->second.add_stmt = add_stmt;
	    any = true;
	  }
	else if (auto *this_add = dyn_cast <gassign *> (use_stmt))
	  {
	    // an add
	    gcc_assert (gimple_assign_rhs_code (this_add) == PLUS_EXPR);
	    tree opcode_arg = gimple_assign_rhs1 (this_add);
	    bool is_op2 = opcode_arg != ssa_var;
	    gcc_assert (!is_op2
			|| gimple_assign_rhs2 (this_add) == ssa_var);
	    
	    auto [iter, inserted] = map.insert ({this_add, use_info (opcode_stmt, add_stmt, is_op2)});
	    gcc_assert (inserted);

	    bool used = self (self, opcode_stmt, gimple_get_lhs (this_add), this_add);
	    map.find (this_add)->second.used = used;
	    count += used;
	  }
	else if (gimple_code (use_stmt) != GIMPLE_DEBUG)
	  {
	    debug_gimple_stmt (opcode_stmt);
	    debug_gimple_stmt (add_stmt);
	    debug_gimple_stmt (use_stmt);
	    gcc_assert (false);
	  }
      }
    return count + unsigned (any);
  };

  tree synth_opcode_decl = rvtt_get_insn_data (rvtt_insn_data::synth_opcode)->decl;
  for (gimple *synth_opcode : synth_opcodes)
    {
      unsigned count = build_graph (build_graph, synth_opcode, gimple_call_lhs (synth_opcode));
      map.find (synth_opcode)->second.count = count;
    }

  for (auto slot = map.begin (); slot != map.end (); ++slot)
    {
      if (!is_gimple_assign (slot->first))
	continue;

      if (!slot->second.used)
	continue;

      if (!slot->second.add_stmt)
	{
	  auto opcode_slot = map.find (slot->second.opcode_stmt);
	  if (opcode_slot->second.count == 1)
	    continue;
	  opcode_slot->second.count--;
	}

      // Sharing a synth_opcode, clone the addition chain parents
      // and synth_opcode.
      // A synth opcode used by more than one add.  Split it.
      unique_id += 2;
      slot->second.count = unique_id;

      auto chain = slot;
      auto add_stmt = slot->first;
      for (;;)
	{
	  gimple *parent_stmt = chain->second.add_stmt;
	  tree ssa_var = make_temp_ssa_name (unsigned_type_node, NULL,
					     parent_stmt ? "sum" : "li");
	  gimple *new_stmt = nullptr;
	  if (parent_stmt)
	    new_stmt = gimple_build_assign
	      (ssa_var, PLUS_EXPR,
	       gimple_assign_rhs1 (parent_stmt),
	       gimple_assign_rhs2 (parent_stmt));
	  else
	    {
	      parent_stmt = chain->second.opcode_stmt;
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

	  if (chain->second.flag)
	    gimple_assign_set_rhs2 (add_stmt, ssa_var);
	  else
	    gimple_assign_set_rhs1 (add_stmt, ssa_var);
	  update_stmt (add_stmt);

	  if (!chain->second.add_stmt)
	    break;

	  add_stmt = new_stmt;
	  chain = map.find (parent_stmt);
	}

      updated = -1;
    }

  if (updated < 0)
    {
      // Update all the uses of the renumbered adds
      for (auto &slot : map)
	{
	  if (!is_gimple_call (slot.first))
	    continue;
	  if (!slot.second.add_stmt)
	    continue;

	  // A use, see if it needs renumbering
	  if (unsigned id = map.find (slot.second.add_stmt)->second.count)
	    {
	      gimple_call_set_arg (slot.first, slot.second.count + 2, 
				   build_int_cst (unsigned_type_node, id));
	      update_stmt (slot.first);
	    }
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
