/* Pass to generate SFPU synth-opcode and opcode synth sequences for
   currently-non-constant operands.
   Copyright (C) 2026 Tenstorrent Inc.
   Originated by Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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
#define INCLUDE_SET
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "gimple.h"
#include "tree-pass.h"
#include "gimple-iterator.h"
#include "gimple-ssa.h"
#include "tree-phinodes.h"
#include "ssa-iterators.h"
#include "value-range.h"
#include "tree-ssa-propagate.h"
#include "stringpool.h"
#include "tree-ssanames.h"
#include "rvtt.h"


// Excitingly we have a few builtins that need placing close to eachother or
// their uses or their defs.  (By close I mean, in the same BB.)  This is so
// the associated RTL pass can do its work without having to know more than
// simple intra-block value propagation.

// The sfpreadlreg builtin is used for both const regs and var regs (because
// that's the API). The RTL combine pass is relied upon to move const lreg
// reads directly into the instruction using that value (rather than go via a
// temp).  Unfortunately combine only combines within a BB, and sometimes
// late-combine or ira doesn't fix it up. Therefore we have our own rtl
// combine-like pass that runs very early. As with combine, it only works in a
// single BB.  This pass duplicates sfpreadlregs of constant regs into the
// block into the bb that it is used. (RTL DCE will fix up the detritus we
// leave behind.)

// Multiple builtin return values are handled as a multi-width vector (because
// builtins are almost-but-not-quite functions, the regular ABI is not in play
// here).  But the underlying instructions will be parallel sets.  At the
// gimple level we have an sfpselectN to pick one of the results.  Using
// VEC_CONCAT and friends didn't work out. Move each sfpselectN into the same
// BB as its def.

static unsigned
transform (function *fn)
{
  bool changed = false;
  tree cstlreg_decl = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg)->decl;
  tree novalue_decl = rvtt_get_insn_data (rvtt_insn_data::sfpnovalue)->decl;
  tree select2_decl = rvtt_get_insn_data (rvtt_insn_data::sfpselect2)->decl;
  tree select4_decl = rvtt_get_insn_data (rvtt_insn_data::sfpselect4)->decl;
  std::set<gcall *> clones;

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	 !gsi_end_p (gsi); gsi_next (&gsi))
      if (auto *call = dyn_cast <gcall *> (*gsi))
	{
	  tree decl = gimple_call_fndecl (call);
	  
	  bool is_cstlreg = decl == cstlreg_decl;
	  if (is_cstlreg || decl == novalue_decl)
	    {
	      if (clones.find (call) != clones.end ())
		continue;

	      tree arg = nullptr;

	      if (is_cstlreg)
		{
		  arg = gimple_call_arg (call, 0);
		  if (TREE_CODE (arg) != INTEGER_CST
		      || TREE_INT_CST_LOW (arg) < 8)
		    continue;
		}

	      tree lhs = gimple_call_lhs (call);
	      if (!lhs)
		continue;

	      // Clone the call to each use location, if they are different BBs
	      gimple *use;
	      imm_use_iterator use_iter;

	      FOR_EACH_IMM_USE_STMT (use, use_iter, lhs)
		{
		  if (gimple_bb (use) == bb)
		    continue;

		  // If this triggers, we'll need to handle it.
		  gcc_assert (!is_a <gphi *> (use));

		  gcall *clone = gimple_build_call (decl, is_cstlreg ? 1 : 0);
		  tree ssa_var = make_ssa_name_fn (fn, TREE_TYPE (lhs), clone);
		  SET_SSA_NAME_VAR_OR_IDENTIFIER (ssa_var, DECL_NAME (decl));

		  gimple_call_set_lhs (clone, ssa_var);
		  if (is_cstlreg)
		    gimple_call_set_arg (clone, 0, arg);
		  gimple_set_location (clone, gimple_location (call));
		  auto use_gsi = gsi_for_stmt (use);
		  gsi_insert_before (&use_gsi, clone, GSI_SAME_STMT);

		  // replace use
		  use_operand_p use_p;
		  FOR_EACH_IMM_USE_ON_STMT (use_p, use_iter)
		    propagate_value (use_p, ssa_var);
		  update_stmt (use);

		  clones.insert (clone);
		  changed = true;
		}

	      continue;
	    }

	  if (decl == select2_decl || decl == select4_decl)
	    {
	      if (clones.find (call) != clones.end ())
		continue;

	      tree res = gimple_call_lhs (call);
	      if (!res)
		continue;

	      auto arg = gimple_call_arg (call, 0);
	      gcc_assert (TREE_CODE (arg) == SSA_NAME);
	      auto *def = SSA_NAME_DEF_STMT (arg);

	      // If it's not a builtin call, then we'll need to chase
	      // further, which will get progressively more complex.
	      gcc_assert (is_a <gcall *> (def));

	      if (gimple_bb (def) == bb)
		continue;

	      // copy the selectN to just after the def stmt
	      gcall *clone = gimple_build_call (decl, 2);
	      tree ssa_var = make_ssa_name_fn (fn, TREE_TYPE (res), clone);
	      SET_SSA_NAME_VAR_OR_IDENTIFIER (ssa_var, DECL_NAME (decl));

	      gimple_call_set_lhs (clone, ssa_var);
	      gimple_call_set_arg (clone, 0, arg);
	      gimple_call_set_arg (clone, 1, gimple_call_arg (call, 1));
	      gimple_set_location (clone, gimple_location (call));
	      auto def_gsi = gsi_for_stmt (def);
	      gsi_insert_after (&def_gsi, clone, GSI_SAME_STMT);

	      gimple *use;
	      imm_use_iterator use_iter;

	      // replace select uses
	      FOR_EACH_IMM_USE_STMT (use, use_iter, res)
		{
		  use_operand_p use_p;

		  FOR_EACH_IMM_USE_ON_STMT (use_p, use_iter)
		    SET_USE (use_p, ssa_var);
		  update_stmt (use);
		}
	      clones.insert (clone);
	      changed = true;
	    }
	}

  return changed ? TODO_update_ssa : 0;
}

namespace {

const pass_data pass_data_rvtt_unspec_prop_ssa =
{
  GIMPLE_PASS, /* type */
  "rvtt_unspec_prop", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_unspec_prop_ssa : public gimple_opt_pass
{
public:
  pass_rvtt_unspec_prop_ssa (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_unspec_prop_ssa, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }

  virtual unsigned execute (function *fn) override
  {
    return transform (fn);
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_unspec_prop_ssa (gcc::context *ctxt)
{
  return new pass_rvtt_unspec_prop_ssa (ctxt);
}
