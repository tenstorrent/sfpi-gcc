/* Pass to give SFPU special builtins block-local defs and uses.
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

/* A few SFPU builtins must sit in the same basic block as their uses
   or their defs, because the RTL passes that consume them perform only
   intra-block value propagation.  This pass restores that locality
   invariant late in the GIMPLE pipeline, after CSE/PRE and code motion
   may have broken it.  Two transformations are performed:

   1. Sinking constant-register reads (SFPREADLREG of a constant LREG,
      index >= 8) and NOVALUE markers to their use blocks.  SFPREADLREG
      serves both variable and constant registers; for constant
      registers the early RTL combine-like pass (rtl-rvtt-unspec.cc)
      folds the read directly into the consuming instruction, but --
      like the generic combiner it is modeled on -- it works only
      within a block.  So each cross-block use gets its own clone of
      the read inserted immediately before it; the original becomes
      dead and RTL DCE sweeps it up.

   2. Hoisting multi-result selectors (SFPSELECT2/SFPSELECT4) to their
      definition blocks.  A builtin returning multiple vector values
      models the result as one wide vector, and a selector call picks
      out one element (the underlying instruction is a parallel set;
      VEC_CONCAT-style modeling did not work out).  Expansion of the
      wide def and its selector must see each other, so a clone of the
      selector is placed immediately after the defining call and all
      the selector's uses are redirected to the clone.

   Both transforms only clone/move calls -- values are never changed --
   so the pass is idempotent, and cloned calls are excluded from
   rescanning within the run.  */


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
#include "gimple-pretty-print.h"
#include "gimple-ssa.h"
#include "tree-phinodes.h"
#include "ssa-iterators.h"
#include "value-range.h"
#include "tree-ssa-propagate.h"
#include "stringpool.h"
#include "tree-ssanames.h"
#include "rvtt.h"


/* Apply both locality transforms across FN; returns TODO_update_ssa if
   anything was cloned.  */

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
		  if (TREE_INT_CST_LOW (arg) < 8)
		    continue;
		}

	      tree lhs = gimple_call_lhs (call);
	      if (!lhs)
		continue;

	      if (dump_file)
		{
		  fprintf (dump_file, "\nSinking reg read\n");
		  print_gimple_stmt (dump_file, call, 0);
		}

	      // Clone the call to each use location, if they are different BBs
	      gimple *use;
	      imm_use_iterator use_iter;

	      FOR_EACH_IMM_USE_STMT (use, use_iter, lhs)
		{
		  if (gimple_bb (use) == bb)
		    {
		      if (dump_file)
			{
			  fprintf (dump_file, "Use is in same BB\n");
			  print_gimple_stmt (dump_file, use, 2);
			}
		      continue;
		    }

		  if (is_a <gphi *> (use))
		    {
		      // FIXME: We should sink it to the incoming edge's src bb?
		      if (dump_file)
			{
			  fprintf (dump_file, "Use is a PHI\n");
			  print_gimple_stmt (dump_file, use, 2);
			}
		      continue;
		    }

		  gcall *clone = gimple_build_call (decl, is_cstlreg ? 1 : 0);
		  tree ssa_var = make_ssa_name_fn (fn, TREE_TYPE (lhs), clone);
		  SET_SSA_NAME_VAR_OR_IDENTIFIER (ssa_var, DECL_NAME (decl));

		  gimple_call_set_lhs (clone, ssa_var);
		  if (is_cstlreg)
		    gimple_call_set_arg (clone, 0, arg);
		  gimple_set_location (clone, gimple_location (call));
		  auto use_gsi = gsi_for_stmt (use);
		  gsi_insert_before (&use_gsi, clone, GSI_SAME_STMT);

		  if (dump_file)
		    {
		      fprintf (dump_file, "Inserted\n");
		      print_gimple_stmt (dump_file, clone, 2);
		      fprintf (dump_file, "before\n");
		      print_gimple_stmt (dump_file, use, 2);
		    }

		  // replace use
		  use_operand_p use_p;
		  FOR_EACH_IMM_USE_ON_STMT (use_p, use_iter)
		    propagate_value (use_p, ssa_var);
		  if (dump_file)
		    {
		      fprintf (dump_file, "Updated\n");
		      print_gimple_stmt (dump_file, use, 2);
		    }
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

	      if (dump_file)
		{
		  fprintf (dump_file, "\nHoisting\n");
		  print_gimple_stmt (dump_file, call, 0);
		}

	      auto arg = gimple_call_arg (call, 0);
	      gcc_assert (TREE_CODE (arg) == SSA_NAME);
	      auto *def = SSA_NAME_DEF_STMT (arg);

	      // If it's not a builtin call, then we'll need to chase
	      // further, which will get progressively more complex.
	      gcc_assert (is_a <gcall *> (def));

	      if (gimple_bb (def) == bb)
		{
		  if (dump_file)
		    {
		      fprintf (dump_file, "\nDefinition is in same BB\n");
		      print_gimple_stmt (dump_file, def, 0);
		    }
		  continue;
		}

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

	      if (dump_file)
		{
		  fprintf (dump_file, "Inserted\n");
		  print_gimple_stmt (dump_file, clone, 2);
		  fprintf (dump_file, "after\n");
		  print_gimple_stmt (dump_file, def, 2);
		}

	      gimple *use;
	      imm_use_iterator use_iter;

	      // replace select uses
	      FOR_EACH_IMM_USE_STMT (use, use_iter, res)
		{
		  use_operand_p use_p;

		  FOR_EACH_IMM_USE_ON_STMT (use_p, use_iter)
		    SET_USE (use_p, ssa_var);
		  if (dump_file)
		    {
		      fprintf (dump_file, "Updated\n");
		      print_gimple_stmt (dump_file, use, 2);
		    }
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
  OPTGROUP_OTHER, /* optinfo_flags */
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
