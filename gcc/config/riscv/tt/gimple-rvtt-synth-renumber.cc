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
  std::unordered_map<gimple *, std::pair<gimple *, unsigned>> map;
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
	map.insert ({def->call, {nullptr, 0}});
      // Insert all the uses
      for (auto *use = def_use.uses; use; use = use->next)
	map.insert ({use->call, {nullptr, use->pos}});

      // Process all the uses, nearly all should meet an add, which in turn
      // should meet a def. (We won't meet an add is cprop has
      // determined the addedn is zero, Huh? why is the nonimm not imm
      // then?
      unsigned num_adds = 0;
      tree synth_opcode_decl = rvtt_get_insn_data (rvtt_insn_data::synth_opcode)->decl;
      for (auto *use = def_use.uses; use; use = use->next)
	{
	  if (map.find (use->call)->second.first)
	    // Already met this processing another one (so they share
	    // the add).
	    continue;

	  tree arg = gimple_call_arg (use->call, use->pos + 1);
	  gcc_assert (TREE_CODE (arg) == SSA_NAME);
	  gimple *def_stmt = SSA_NAME_DEF_STMT (arg);
	  gassign *add = dyn_cast <gassign *> (def_stmt);
	  if (!add)
	    {
	      // sfpxicmps and friends cause the add to be elided by
	      // setting the mask to zero. Things get reconstituted in
	      // the expand pass.
	      // FIXME: That's hokey as it prevents renumbering we're
	      // trying to achieve here. We should fix it.
	      gcc_assert (is_gimple_call (def_stmt)
			  && gimple_call_fndecl (def_stmt) == synth_opcode_decl);
	      complex = true;
	      break;
	    }

	  gcc_assert (gimple_assign_rhs_code (add) == PLUS_EXPR);

	  // Find all the uses of the ssa-var (which will include
	  // this one).
	  {
	    imm_use_iterator iter;
	    gimple *stmt;
	    // FIXME: Can we use quick iterator here?
	    FOR_EACH_IMM_USE_STMT (stmt, iter, arg)
	      {
		if (gimple_code (stmt) == GIMPLE_DEBUG)
		  continue;
		auto slot = map.find (stmt);
		if (slot == map.end())
		  {
		    // ASSERT?
		    complex = true;
		    break;
		  }
		else
		  slot->second.first = add;
	      }
	    if (complex)
	      break;
	  }

	  // Find the synth_opcode providing the add's input, update
	  // that
	  bool op2 = false;
	  tree opcode = gimple_assign_rhs1 (add);
	  gcall *synth = nullptr;
	  if (TREE_CODE (opcode) == SSA_NAME)
	    synth = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (opcode));
	  if (!synth || gimple_call_fndecl (synth) != synth_opcode_decl)
	    {
	      op2 = true;
	      opcode = gimple_assign_rhs2 (add);
	      if (TREE_CODE (opcode) == SSA_NAME)
		synth = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (opcode));
	      else
		{
		  debug_gimple_stmt (use->call);
		  debug_gimple_stmt (add);
		  gcc_assert (false);
		}
	    }
	  auto synth_slot = map.find (synth);
	  gcc_assert (synth_slot != map.end ());
	  synth_slot->second.second++;

	  // Insert add into the map -- it cannot already be there
	  auto [iter, inserted] = map.insert ({add, {synth, op2}});
	  gcc_assert (inserted);
	  num_adds++;
	}

      if (complex)
	// Too complex, don't touch.
	continue;

      if (num_adds == 1)
	continue;

      // Multiple adds, (maybe) renumber
      unsigned renumbered = false;
      unsigned unique_id = synths.size ();
      bool first = true;
      for (auto &mapping : map)
	{
	  if (!is_gimple_call (mapping.first))
	    continue;
	  if (gimple_call_fndecl (mapping.first) != synth_opcode_decl)
	    continue;

	  if (!mapping.second.second)
	    // no uses
	    continue;

	  // We do not need to renumber the first use.
	  if (first && mapping.second.second == 1)
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

	      if (add.second.second)
		gimple_assign_set_rhs2 (add.first, new_op);
	      else
		gimple_assign_set_rhs1 (add.first, new_op);
	      update_stmt (add.first);

	      add.second.second = unique_id << 1;
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
	      if (gimple_call_fndecl (mapping.first) == synth_opcode_decl)
		continue;

	      // A use, see if it needs renumbering
	      auto add = map.find (mapping.second.first);
	      gcc_assert (add != map.end ());
	      if (unsigned id = add->second.second >> 1)
		gimple_call_set_arg (mapping.first, mapping.second.second + 2, 
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
