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
#include <string.h>
#include <vector>
#include <map>
#include <iostream>
#include <tuple>
#include "config/riscv/sfpu.h"

using namespace std;

// This pass optimizes 2 things:
// 1) The outermost pushc/popc: remove pushc, turn popc into encc
// 2) Tail popc: if 2 popcs occur without intervening instructions and the
//    inner pushc/popc did not have a compc in between, the inner pushc/popc
//    can be removed.  Tricky because you have to track the prior popc
//
// For now, this handles only the case of all the sfpu push/pop instructions
// falling within one BB which covers most of the existing kernels.
static void transform (function *fun)
{
  vector<tuple<bool, gimple_stmt_iterator>> stack;
  basic_block bb, sfpu_bb;
  gimple_stmt_iterator gsi, prior_pushc, prior_popc;
  bool prior_removable = false;

  sfpu_bb = nullptr;
  FOR_EACH_BB_FN (bb, fun) {
    gsi = gsi_start_bb (bb);
    while (!gsi_end_p (gsi))
      {
	gcall *stmt;
	const riscv_sfpu_insn_data *insnd;
	gimple *g = gsi_stmt (gsi);
	if (riscv_sfpu_p(&insnd, &stmt, gsi))
	  {
	    if (insnd->id == riscv_sfpu_insn_data::sfppushc ||
		insnd->id == riscv_sfpu_insn_data::sfppopc)
	      {
		if (sfpu_bb != nullptr)
		  {
		    // Multiple BBs contain sfpu instructions, bail
		    return;
		  }
		sfpu_bb = bb;
		break;
	      }
	  }
	gsi_next (&gsi);
      }
  }

  if (sfpu_bb == nullptr)
    {
      // Got nuthin
      return;
    }

  // Find all function calls
  gsi = gsi_start_bb (sfpu_bb);
  while (!gsi_end_p (gsi))
    {
      gcall *stmt;
      const riscv_sfpu_insn_data *insnd;
      if (riscv_sfpu_p(&insnd, &stmt, gsi))
	{
	  if (insnd->id == riscv_sfpu_insn_data::sfppushc)
	    {
	      prior_removable = false;
	      if (stack.size() == 0)
		{
		  // Remove outermost pushc
		  gimple *stmt = gsi_stmt (gsi);
		  unlink_stmt_vdef(stmt);
		  gsi_remove(&gsi, true);
		  release_defs(stmt);

		  stack.push_back(make_tuple(false, gsi));
		  // Avoid the gsi_next at the end since we removed the inst
		  continue;
		}
	      else
		{
		  stack.push_back(make_tuple(false, gsi));
		}
	    }
	  else if (insnd->id == riscv_sfpu_insn_data::sfpcompc)
	    {
	      // Set compc to true for current pushc
	      if (stack.size() == 0) {
		error("Error: malformed program, sfpcompc outside of pushc/popc - exiting!");
	      }

	      prior_removable = false;
	      stack.back() = make_tuple(true, get<1>(stack.back()));
	    }
	  else if (insnd->id == riscv_sfpu_insn_data::sfppopc)
	    {
	      if (stack.size() == 0) {
		error("Error: malformed program, popc without matching pushc - exiting!");
	      }

	      if (prior_removable) {
		gimple *stmt = gsi_stmt (prior_pushc);
		unlink_stmt_vdef(stmt);
		gsi_remove(&prior_pushc, true);
		release_defs(stmt);

		stmt = gsi_stmt (prior_popc);
		unlink_stmt_vdef(stmt);
		gsi_remove(&prior_popc, true);
		release_defs(stmt);
	      }

	      // Not removable if we saw a compc
	      prior_removable = !get<0>(stack.back()); 
	      prior_pushc = get<1>(stack.back());
	      prior_popc = gsi;

	      stack.pop_back();
	      if (stack.size() == 0)
		{
		  // Replace outermost popc with encc
		  const riscv_sfpu_insn_data *new_insnd =
		    riscv_sfpu_get_insn_data(riscv_sfpu_insn_data::sfpencc);
		  gimple *new_stmt = gimple_build_call(new_insnd->decl, 2, size_int(3), size_int(10));
		  if (new_stmt == nullptr) {
		    gcc_unreachable();
		  }

		  move_ssa_defining_stmt_for_defs (new_stmt, stmt);
		  gimple_set_vuse (new_stmt, gimple_vuse (stmt));
		  gimple_set_vdef (new_stmt, gimple_vdef (stmt));
		  gimple_set_location (new_stmt, gimple_location (stmt));
		  gimple_set_block (new_stmt, gimple_block (stmt));
		  gsi_replace(&gsi, new_stmt, true);
		  update_stmt (new_stmt);
		  prior_removable = false;
		}
	    }
	  else
	    {
	      // Could be smarter about the non-__builtin_riscv_sfp
	      // calls, but bail if anything else comes in to be safe
	      // "Other" instructions
	      prior_removable = false;
	    }
	}

      gsi_next (&gsi);
    }
}

namespace {

const pass_data pass_data_riscv_sfpu_cc =
{
  GIMPLE_PASS, /* type */
  "riscv_sfpu_cc", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_riscv_sfpu_cc : public gimple_opt_pass
{
public:
  pass_riscv_sfpu_cc (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_riscv_sfpu_cc, ctxt)
  {}

  virtual unsigned int execute (function *);
}; // class pass_riscv_sfpu_cc

} // anon namespace

/* Entry point to riscv_sfpu_cc pass.	*/
unsigned int
pass_riscv_sfpu_cc::execute (function *fun)
{
  transform (fun);
  return 0;
}

gimple_opt_pass *
make_pass_riscv_sfpu_cc (gcc::context *ctxt)
{
  return new pass_riscv_sfpu_cc (ctxt);
}
