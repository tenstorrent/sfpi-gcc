/* Pass to tag SFPU non-immediate insns for later processing
   Copyright (C) 2022 Free Software Foundation, Inc.
   Contributed by Paul Keller (pkeller@tenstorrent.com).

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

using namespace std;

// This pass finds builtins with non-immediate values and:
//  - emits the prologue (shift, mask, synth_opcode, add)
//  - creates an SSA dependency for the insn on the prologue
//  - links the synth_opcode to the builtin via a unique id (this id
// also prevents CSE combining unrelated synth_opcodes)
//
// The nonimm_pos field in insnd points to the non-immediate argument in the
// insn.  The nonimm_pos+1 argument is where the prologue gets used and the
// nonimmm_pos+2 is where the synth_opcode dependency is stored.
//
// This pass must run early in the pipe, but after inlining - before
// loop unrolling.  This allows makes it easy to create a 1-1 mapping
// from synth_opcode to non-immediate insns.  After loop unrolling,
// there can be multiple synth_opcodes with the same ids.
// Unfortunately, the pass by which all constant folding, evaluation,
// etc is done occurs after loop unrolling. That means this pass flags
// some insns as non-imm which will eventually be determined to be
// imm. This gets cleaned up in the nonimm-expand pass.

static void
transform (function *fun)
{
  basic_block bb;

  unsigned int synth_id = 0;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	 !gsi_end_p (gsi); gsi_next(&gsi))
      {
	gcall *stmt;
	const rvtt_insn_data *insnd;

	if (rvtt_p (&insnd, &stmt, gsi)
	    && insnd->nonimm_pos != -1)
	  {
	    tree immarg = gimple_call_arg (stmt, insnd->nonimm_pos);
	    if (TREE_CODE (immarg) == INTEGER_CST)
	      continue;
	    synth_id += 2; // We may need to insert another one later
	    tree sum = rvtt_emit_nonimm_prologue (synth_id, insnd, stmt, gsi);

	    // Update insn to make insnd->nonimm_pos+1 contain the sum
	    gimple_call_set_arg (stmt, insnd->nonimm_pos + 1, sum);
	    // Save unique_id in insn's id field
	    gimple_call_set_arg (stmt, insnd->nonimm_pos + 2,
				build_int_cst (integer_type_node, synth_id));
	    update_stmt (stmt);
	  }
      }

  if (synth_id)
    update_ssa (TODO_update_ssa);
}

namespace {

const pass_data pass_data_rvtt_nonimm_tag =
{
  GIMPLE_PASS, /* type */
  "rvtt_nonimm_tag", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_nonimm_tag : public gimple_opt_pass
{
public:
  pass_rvtt_nonimm_tag (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_nonimm_tag, ctxt)
  {}

  virtual unsigned int execute (function *);
}; // class pass_rvtt_nonimm_tag

} // anon namespace

/* Entry point to rvtt_nonimm_tag pass.	*/
unsigned int
pass_rvtt_nonimm_tag::execute (function *fun)
{
  if (TARGET_RVTT)
    transform (fun);
  return 0;
}

gimple_opt_pass *
make_pass_rvtt_nonimm_tag (gcc::context *ctxt)
{
  return new pass_rvtt_nonimm_tag (ctxt);
}
