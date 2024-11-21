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
#include "target.h"
#include "rtl.h"
#include "tree.h"
#include "gimple.h"
#include "cfghooks.h"
#include "tree-pass.h"
#include "ssa.h"
#include "cgraph.h"
#include "gimple-pretty-print.h"
#include "diagnostic-core.h"
#include "fold-const.h"
#include "trans-mem.h"
#include "stor-layout.h"
#include "print-tree.h"
#include "cfganal.h"
#include "gimple-fold.h"
#include "tree-eh.h"
#include "gimple-iterator.h"
#include "gimplify-me.h"
#include "gimple-walk.h"
#include "tree-cfg.h"
#include "tree-ssa-loop-manip.h"
#include "tree-ssa-loop-niter.h"
#include "tree-into-ssa.h"
#include "tree-dfa.h"
#include "tree-ssa.h"
#include "except.h"
#include "cfgloop.h"
#include "tree-ssa-propagate.h"
#include "value-prof.h"
#include "tree-inline.h"
#include "tree-ssa-live.h"
#include "omp-general.h"
#include "omp-expand.h"
#include "tree-cfgcleanup.h"
#include "gimplify.h"
#include "attribs.h"
#include "selftest.h"
#include "opts.h"
#include "asan.h"
#include "profile.h"
#include "config/riscv/rvtt.h"

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

using namespace std;

// This pass finds insns w/ non-immediate values and:
//  - emits the prologue (mask, load immediate opcode, add)
//  - creates an SSA dependency for the insn on the prologue
//  - links the load_immediate value to the insn (via SSA)
//
// The load_immediate value at this point is a unique integer to make it
// easy to identify which non-imm insns are dependent on the load_immediate.
// (this could have been done just w/ SSA...).  The value will eventually
// be replaced by the opcode+mod(etc)+register numbers used for the insn.
//
// The nonimm_pos field in insnd points to the non-immediate argument in the
// insn.  The nonimm_pos+1 argument is where the prologue gets used and the
// nonimmm_pos+2 is where the load_immediate dependency is stored.
//
// This pass must run early in the pipe - before loop unrolling.  This allows
// makes it easy to create a 1-1 mapping from load_immediate to non-immediate
// insn.  After loop unrolling, multiple related insns will be mapped to the
// same non-immediate, allowing re-use of the prologue.  Unfortunately, the
// pass by which all constant folding, evaluation, etc is done occurs after
// loop unrolling.  That means this pass flags some insns as non-imm which
// will eventually be determined to be imm.  This gets cleaned up in the
// nonimm-expand pass.
static void
transform (function *fun)
{
  DUMP("Tag-nonimm pass on: %s\n", function_name(fun));

  basic_block bb;
  gimple_stmt_iterator gsi;

  bool updated = false;
  unsigned int unique_id = 2;
  FOR_EACH_BB_FN (bb, fun)
    {
      gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gcall *stmt;
	  const rvtt_insn_data *insnd;

	  if (rvtt_p(&insnd, &stmt, gsi) &&
	      insnd->nonimm_pos != -1)
	    {
	      tree immarg = gimple_call_arg(stmt, insnd->nonimm_pos);
	      if (TREE_CODE(immarg) == SSA_NAME)
		{
		  DUMP("  nonimm %s, id %d\n", insnd->name, unique_id);
		  tree sum = rvtt_emit_nonimm_prologue(unique_id, insnd, stmt, gsi);

		  // Update insn to make insnd->nonimm_pos+1 contain the sum
		  gimple_call_set_arg(stmt, insnd->nonimm_pos + 1, sum);
		  // Save unique_id in insn's id field
		  gimple_call_set_arg(stmt, insnd->nonimm_pos + 2,
				      build_int_cst(integer_type_node, unique_id));
		  update_stmt (stmt);

		  unique_id += 2; // leave room for inserted insns later
		  updated = true;
		}
	    }

	  gsi_next(&gsi);
	}
    }

  if (updated)
    {
      update_ssa (TODO_update_ssa);
    }
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
