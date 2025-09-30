/* Pass to generate nullify non immediates that turned out to be immediate.
   Copyright (C) 2025 Tenstorrent Inc.
   Written by Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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
#include "diagnostic-core.h"
#include "tree-pass.h"
#include "ssa.h"
#include "gimple-iterator.h"
#include "tree-into-ssa.h"
#include "rvtt.h"

static unsigned
transform (function *fn)
{
  basic_block bb;

  bool updated = false;
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	 !gsi_end_p (gsi); gsi_next (&gsi))
      {
	gcall *stmt;
	const rvtt_insn_data *insnd;

	if (!rvtt_p (&insnd, &stmt, gsi))
	  continue;

	if (insnd->nonimm_pos != -1)
	  {
	    if (TREE_CODE (gimple_call_arg (stmt, insnd->nonimm_pos)) == INTEGER_CST
		&& TREE_CODE (gimple_call_arg (stmt, insnd->nonimm_pos + 1)) != INTEGER_CST)
	      {
		// It turned out to be a constant.
		gimple_call_set_arg (stmt, 0, null_pointer_node);
		gimple_call_set_arg (stmt, insnd->nonimm_pos + 1, integer_zero_node);
		gimple_call_set_arg (stmt, insnd->nonimm_pos + 2, integer_zero_node);
		update_stmt (stmt);
		updated = true;
	      }
	    continue;
	  }

	if (insnd->id == rvtt_insn_data::ttinsn)
	  {
	    if (gimple_call_arg (stmt, 0) != null_pointer_node)
	      {
		if (TREE_CODE (gimple_call_arg (stmt, 2)) == INTEGER_CST)
		  {
		    // It too turned out to be known now.
		    gimple_call_set_arg (stmt, 0, null_pointer_node);
		    update_stmt (stmt);
		    updated = true;
		  }
		else if (integer_nonzerop (gimple_call_arg (stmt, 1)))
		  // User required it to be statically known.
		  warning_at (gimple_location (stmt), 0, "ttinsn is not statically known");
	      }
	    continue;
	  }
      }

  return updated ? TODO_update_ssa : 0;
}

namespace {

const pass_data pass_data_rvtt_synth_nullify =
{
  GIMPLE_PASS, /* type */
  "rvtt_synth_nullify", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_synth_nullify : public gimple_opt_pass
{
public:
  pass_rvtt_synth_nullify (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_synth_nullify, ctxt)
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
make_pass_rvtt_synth_nullify (gcc::context *ctxt)
{
  return new pass_rvtt_synth_nullify (ctxt);
}
