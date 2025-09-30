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
#include "tree-into-ssa.h"
#include "rvtt.h"

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

static void
transform (function *fn)
{
  basic_block bb;

  bool updated = false;
  unsigned int synth_id = 0;
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
	    tree immarg = gimple_call_arg (stmt, insnd->nonimm_pos);
	    if (TREE_CODE (immarg) == INTEGER_CST)
	      gimple_call_set_arg (stmt, 0, null_pointer_node);
	    else
	      {
		synth_id += 2; // We may need to insert another one later
		tree sum = rvtt_emit_nonimm_prologue (synth_id, insnd, stmt, gsi);

		// Update insn to make insnd->nonimm_pos+1 contain the sum
		gimple_call_set_arg (stmt, insnd->nonimm_pos + 1, sum);
		// Save unique_id in insn's id field
		gimple_call_set_arg (stmt, insnd->nonimm_pos + 2,
				     build_int_cst (integer_type_node, synth_id));
	      }
	  }
	else if (insnd->id == rvtt_insn_data::ttinsn
		 && TREE_CODE (gimple_call_arg (stmt, 2)) == INTEGER_CST)
	  gimple_call_set_arg (stmt, 0, null_pointer_node);
	else
	  continue;

	update_stmt (stmt);
	updated = true;
      }

  if (updated)
    update_ssa (TODO_update_ssa);
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
    transform (fn);
    return 0;
  }
};

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_synth_split (gcc::context *ctxt)
{
  return new pass_rvtt_synth_split (ctxt);
}
