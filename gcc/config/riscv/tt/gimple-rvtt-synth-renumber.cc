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

static bool
transform (function *fn)
{
  struct synth {
    gcall *call = nullptr;
    int pos = -1;
    synth *next = nullptr;

    synth (gcall *c, int p, synth *n)
      : call (c), pos (p), next (n) {}
    synth (gcall *c, synth *n)
      : synth (c, -1, n) {}
  };
  struct def_use
  {
    synth *defs;
    synth *uses;
  };
  std::vector<def_use> synths;

  basic_block bb;
  bool updated = false;

  // Find all the defs & uses
  FOR_EACH_BB_FN (bb, fn)
    for (auto gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
      {
	gcall *stmt;
	const rvtt_insn_data *insnd;

	if (!rvtt_p (&insnd, &stmt, gsi))
	  continue;
	unsigned id;
	if (insnd->id == rvtt_insn_data::synth_opcode)
	  id = TREE_INT_CST_LOW (gimple_call_arg (stmt, 1));
	else if (insnd->nonimm_pos < 0)
	  continue;
	else if (TREE_CODE (gimple_call_arg (stmt, insnd->nonimm_pos)) == INTEGER_CST)
	  {
	    // It's now a constant, zap the synth arg.
	    gimple_call_set_arg (stmt, insnd->nonimm_pos + 1, integer_zero_node);
	    update_stmt (stmt);
	    updated = true;
	    continue;
	  }
	else
	  id = TREE_INT_CST_LOW (gimple_call_arg (stmt, insnd->nonimm_pos + 2));

	if (id >= synths.size ())
	  synths.resize (id + 1);
	void *node = ggc_alloc<synth> ();
	auto &elt = synths[id];
	if (insnd->id == rvtt_insn_data::synth_opcode)
	  elt.defs = ::new (node) synth (stmt, elt.defs);
	else
	  elt.uses = ::new (node) synth (stmt, insnd->nonimm_pos, elt.uses);
      }

  // Map a using insn to a defining insn, and count the uses of this
  // insn.  We throw all insn of interest in here.
  // synth_opcode -> nullptr, num-adds
  // add -> synth_opcode, (remapped id or 0) << 1 | op2
  // nonimm_use -> add, nonimm-pos
  struct use_info
  {
    gimple *opcode_stmt = nullptr;
    gimple *add_stmt = nullptr;
    unsigned count = 0; // opcode_stmt: count of adds then new id
                        // use stmt: nonimm pos
    bool flag = false;  // add:rhs2

    use_info () = default;
    use_info (unsigned pos) : count (pos) {}
    use_info (gimple *op, gimple *def, bool f)
      : opcode_stmt (op), add_stmt (def != op ? def : nullptr), flag (f) {}
  };
  std::unordered_map<gimple *, use_info> map;
  for (auto &def_use : synths)
    {
      if (!def_use.uses || !def_use.uses->next)
	// No, or exactly one use.
	continue;
      gcc_assert (def_use.defs);
      bool complex = false;

      map.clear ();
      // Insert all the defs
      for (auto *def = def_use.defs; def; def = def->next)
	map.insert ({def->call, use_info ()});
      // Insert all the uses
      unsigned num_uses = 0;
      for (auto *use = def_use.uses; use; use = use->next)
	{
	  map.insert ({use->call, use_info (use->pos)});
	  num_uses++;
	}

      // Process all the uses, nearly all should meet an add, which in turn
      // should meet a def.  There are two other case.
      // 1) See below abour sfpxicmps and friends
      // 2) a partially unrolled loop can cascade adds after an
      // ivopt.  We need to record the whole chain in order to split it.
      unsigned num_adds = 0;
      unsigned total_adds = 0;
      tree synth_opcode_decl = rvtt_get_insn_data (rvtt_insn_data::synth_opcode)->decl;
      auto build_graph = [&] (auto &self, gimple *opcode_stmt, gimple *def_stmt, bool is_indirect)
      {
	tree ssa_var = gimple_get_lhs (def_stmt);
	imm_use_iterator iter;
	gimple *use_stmt;

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
		    debug_gimple_stmt (def_stmt);
		    gcc_assert (false);
		  }
		if (opcode_stmt == def_stmt)
		  // sfpxicmps and friends cause the add to be elided by
		  // setting the mask to zero. Things get reconstituted in
		  // the expand pass.
		  // FIXME: That's hokey as it prevents renumbering we're
		  // trying to achieve here. We should do better.
		  return true;

		slot->second.opcode_stmt = opcode_stmt;
		slot->second.add_stmt = def_stmt;
		num_uses--;
	      }
	    else if (auto *add_stmt = dyn_cast <gassign *> (use_stmt))
	      {
		// an add
		gcc_assert (gimple_assign_rhs_code (add_stmt) == PLUS_EXPR);
		tree opcode_arg = gimple_assign_rhs1 (add_stmt);
		bool is_op2 = opcode_arg != ssa_var;
		gcc_assert (!is_op2 || gimple_assign_rhs2 (add_stmt) == ssa_var);
		if (opcode_stmt != def_stmt)
		  {
		    // If this is an add of an add, that's the ivopt chain
		    gcc_assert (!is_op2
				&& TREE_CODE (gimple_assign_rhs2 (add_stmt)) == INTEGER_CST);
		    // Treat as complex for now
		    return true;
		  }
		auto [iter, inserted] = map.insert ({add_stmt, use_info (opcode_stmt, def_stmt, is_op2)});
		gcc_assert (inserted);
		num_adds++;

		if (self (self, opcode_stmt, add_stmt, opcode_stmt != def_stmt))
		  return true;
	      }
	    else if (gimple_code (use_stmt) != GIMPLE_DEBUG)
	      {
		debug_gimple_stmt (opcode_stmt);
		debug_gimple_stmt (def_stmt);
		debug_gimple_stmt (use_stmt);
		gcc_assert (false);
	      }
	  }
	return false;
      };

      for (auto *def = def_use.defs; def; def = def->next)
	{
	  gimple *opcode_stmt = def->call;

	  num_adds = 0;
	  if (build_graph (build_graph, opcode_stmt, opcode_stmt, false))
	    {
	      complex = true;
	      break;
	    }
	  map.find (opcode_stmt)->second.count += num_adds;
	  total_adds += num_adds;
	}

      // split?
      if (complex)
	// Too complex, don't touch.
	continue;

      // Make sure we found all the uses
      gcc_assert (num_uses == 0);

      if (total_adds == 1)
	// Only one add, no need to split
	continue;

      // Multiple adds, (maybe) renumber
      unsigned renumbered = false;
      unsigned unique_id = synths.size ();
      bool first = true;
      for (auto &mapping : map)
	{
	  if (!is_gimple_call (mapping.first))
	    // An add
	    continue;
	  if (gimple_call_fndecl (mapping.first) != synth_opcode_decl)
	    // A use
	    continue;

	  if (!mapping.second.count)
	    // no adds uses this -- can we get that?
	    continue;

	  // We do not need to renumber the first use.
	  if (first && mapping.second.count == 1)
	    {
	      first = false;
	      continue;
	    }

	  // A synth opcode used by more than one add.  Split it.
	  auto gsi = gsi_for_stmt (mapping.first);
	  for (auto &add : map)
	    {
	      if (!is_gimple_assign (add.first))
		continue;
	      if (add.second.add_stmt)
		gcc_unreachable (); // for the moment

	      if (first)
		{
		  first = false;
		  continue;
		}

	      unique_id += 2;
	      tree new_op = make_temp_ssa_name (unsigned_type_node, NULL, "li");
	      gcall *new_stmt = gimple_build_call (synth_opcode_decl, 2);
	      gimple_call_set_arg (new_stmt, 0, integer_zero_node);
	      gimple_call_set_arg (new_stmt, 1, build_int_cst (unsigned_type_node, unique_id));
	      gimple_call_set_lhs (new_stmt, new_op);
	      gimple_set_location (new_stmt, gimple_location (mapping.first));
	      update_stmt (new_stmt);
	      gsi_insert_after (&gsi, new_stmt, GSI_NEW_STMT);

	      if (add.second.flag)
		gimple_assign_set_rhs2 (add.first, new_op);
	      else
		gimple_assign_set_rhs1 (add.first, new_op);
	      update_stmt (add.first);

	      add.second.count = unique_id;
	      renumbered = true;
	    }
	}
      if (renumbered)
	{
	  // Update all the uses of the renumbered adds
	  for (auto &mapping : map)
	    {
	      if (!is_gimple_call (mapping.first))
		continue;
	      if (!mapping.second.add_stmt)
		{
		  gcc_assert (gimple_call_fndecl (mapping.first) == synth_opcode_decl);
		  continue;
		}

	      // A use, see if it needs renumbering
	      auto add = map.find (mapping.second.add_stmt);
	      gcc_assert (add != map.end ());
	      if (unsigned id = add->second.count)
		gimple_call_set_arg (mapping.first, mapping.second.count + 2, 
				     build_int_cst (unsigned_type_node, id));
	    }
	}
    }

  return updated;
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
    return transform (fn) ? TODO_update_ssa : 0;
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_synth_renumber (gcc::context *ctxt)
{
  return new pass_rvtt_synth_renumber (ctxt);
}
