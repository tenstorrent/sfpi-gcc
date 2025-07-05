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
  int updated = false; // true/false/file-not-found :)

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
	    // It's now a constant, zap the synth arg and pointer
	    gimple_call_set_arg (stmt, 0, null_pointer_node);
	    gimple_call_set_arg (stmt, insnd->nonimm_pos + 1, integer_zero_node);
	    gimple_call_set_arg (stmt, insnd->nonimm_pos + 2, integer_zero_node);
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
    gcall *opcode_stmt = nullptr;
    gassign *add_stmt = nullptr;
    unsigned count = 0; // opcode_stmt: count of adds then new id
                        // add: new-id
                        // use stmt: nonimm pos
    bool flag = false;  // add:rhs2

    use_info () = default;
    use_info (unsigned pos) : count (pos) {}
    use_info (gcall *op, gassign *add, bool f)
      : opcode_stmt (op), add_stmt (add), flag (f) {}
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
      auto build_graph = [&] (auto &self, gcall *opcode_stmt, tree ssa_var, gassign *def_stmt = nullptr)
      {
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
		if (!def_stmt)
		  // sfpxicmps and friends cause the add to be elided by
		  // setting the mask to zero. Things get reconstituted in
		  // the expand pass.
		  // FIXME: That's hokey as it prevents the renumbering we're
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
		gcc_assert (!is_op2
			    || gimple_assign_rhs2 (add_stmt) == ssa_var);

		auto [iter, inserted] = map.insert ({add_stmt, use_info (opcode_stmt, def_stmt, is_op2)});
		gcc_assert (inserted);
		num_adds++;

		if (self (self, opcode_stmt, gimple_get_lhs (add_stmt), add_stmt))
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
	  gcall *opcode_stmt = def->call;

	  num_adds = 0;
	  if (build_graph (build_graph, opcode_stmt, gimple_call_lhs (opcode_stmt)))
	    {
	      complex = true;
	      break;
	    }
	  map.find (opcode_stmt)->second.count += num_adds;
	  total_adds += num_adds;
	}

      if (complex)
	// Too complex, don't touch.
	continue;

      // Make sure we found all the uses
      gcc_assert (num_uses == 0);

      if (total_adds == 1)
	// Only one add, no need to split
	continue;

      // Multiple adds, (maybe) renumber
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
	    // nothin uses this -- can we get that?
	    continue;

	  // We do not need to renumber the first use.
	  if (first && mapping.second.count == 1)
	    {
	      first = false;
	      continue;
	    }

	  // A synth opcode used by more than one add.  Split it.
	  for (auto add = map.begin (); add != map.end(); ++add)
	    {
	      if (!is_gimple_assign (add->first))
		continue;
	      if (first)
		{
		  first = false;
		  continue;
		}

	      unique_id += 2;
	      add->second.count = unique_id;

	      // clone the input chain to add back to the synth_opcode
	      // stmt.
	      auto chain = add;
	      auto add_stmt = add->first;
	      for (;;)
		{
		  gimple *orig_stmt = chain->second.add_stmt;
		  tree ssa_var = make_temp_ssa_name (unsigned_type_node, NULL,
						     orig_stmt ? "sum" : "li");
		  gimple *new_stmt = nullptr;
		  if (orig_stmt)
		    new_stmt = gimple_build_assign
		      (ssa_var, PLUS_EXPR,
		       gimple_assign_rhs1 (chain->second.add_stmt),
		       gimple_assign_rhs2 (chain->second.add_stmt));
		  else
		    {
		      orig_stmt = chain->second.opcode_stmt;
		      gcall *new_call = gimple_build_call (synth_opcode_decl, 2);
		      gimple_call_set_arg (new_call, 0, integer_zero_node);
		      gimple_call_set_arg (new_call, 1, build_int_cst (unsigned_type_node, unique_id));
		      gimple_call_set_lhs (new_call, ssa_var);
		      update_stmt (new_call);
		      new_stmt = new_call;
		    }

		  gimple_set_location (new_stmt, gimple_location (orig_stmt));
		  auto orig_gsi = gsi_for_stmt (orig_stmt);
		  gsi_insert_after (&orig_gsi, new_stmt, GSI_NEW_STMT);

		  if (chain->second.flag)
		    gimple_assign_set_rhs2 (add_stmt, ssa_var);
		  else
		    gimple_assign_set_rhs1 (add_stmt, ssa_var);
		  update_stmt (add_stmt);

		  if (!chain->second.add_stmt)
		    break;

		  add_stmt = new_stmt;
		  chain = map.find (orig_stmt);
		  gcc_assert (chain != map.end ());
		}

	      updated = -1;
	    }
	}

      if (updated < 0)
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
		{
		  gimple_call_set_arg (mapping.first, mapping.second.count + 2, 
				       build_int_cst (unsigned_type_node, id));
		  update_stmt (mapping.first);
		}
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
