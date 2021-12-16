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

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

using namespace std;

//////////////////////////////////////////////////////////////////////////////
// bool in tuple tracks whether or not a COMPC was seen at this stack depth
static void
process_block_stmts(basic_block bb,
		    vector<tuple<bool, gimple_stmt_iterator>> &stack)
{
  gimple_stmt_iterator gsi, prior_pushc, prior_popc;
  bool prior_removable = false;

  // Find all function calls
  gsi = gsi_start_bb (bb);
  while (!gsi_end_p (gsi))
    {
      gcall *stmt;
      const riscv_sfpu_insn_data *insnd;
      if (riscv_sfpu_p(&insnd, &stmt, gsi))
	{
	  if (insnd->id == riscv_sfpu_insn_data::sfppushc)
	    {
	      prior_removable = false;
	      DUMP("PUSHC: stack size %d\n", stack.size());

	      if (stack.size() == 0)
		{
		  DUMP("  removing outermost pushc\n");

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
	      DUMP("POPC: stack size %d\n", stack.size());

	      if (stack.size() == 0) {
		error("Error: malformed program, popc without matching pushc - exiting!");
	      }

	      // Only remove inner PUSHC/POPC if they fall within a bb
	      // since different paths may differ in intervening instructions
	      if (prior_removable &&
		  prior_pushc.bb == prior_popc.bb &&
		  prior_popc.bb == gsi.bb) {
		DUMP("  removing inner PUSHC/POPC\n");
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
		  DUMP("  replacing outermost popc with encc\n");

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
	      DUMP("Intervening %s\n", insnd->name);
	      // Could be smarter about the non-__builtin_riscv_sfp
	      // calls, but bail if anything else comes in to be safe
	      // "Other" instructions
	      prior_removable = false;
	    }
	}

      gsi_next (&gsi);
    }
}

static void
process_block(basic_block bb,
	      vector<bool>& bd,
	      vector<tuple<bool, gimple_stmt_iterator>> stack)
{
  edge_iterator ei;
  edge e;

  // If we hit the same BB multiple times, the stack depth must always be the
  // same.  The liveness pass asserts this.  If this is ever found to not be
  // true, we'll have to bail on optimizing the CC for that BB.

  DUMP("Process block %d\n", bb->index);
  if (!bd[bb->index])
    {
      // Haven't visited this BB before
      process_block_stmts(bb, stack);
      bd[bb->index] = true;

      // When we leave, EDGE_COUNT == 0, stack must be empty
      gcc_assert(EDGE_COUNT(bb->succs) != 0 || stack.size() == 0);

      FOR_EACH_EDGE(e, ei, bb->succs)
	{
	  // When we leave, EDGE_COUNT == 0, stack must be empty
	  process_block(e->dest, bd, stack);
	}
    }
}

// This pass optimizes 2 things:
// 1) The outermost pushc/popc: remove pushc, turn popc into encc
// 2) Tail popc: if 2 popcs occur without intervening instructions and the
//    inner pushc/popc did not have a compc in between, the inner pushc/popc
//    can be removed.  Tricky because you have to track the prior popc.
//    The inner push/pop and the outer pop must all be in the same BB
static void transform (function *fn)
{
  vector<tuple<bool, gimple_stmt_iterator>> stack;
  vector<bool> bd;

  if (lookup_attribute ("always_inline", DECL_ATTRIBUTES (fn->decl)) != NULL)
    {
      // Skip the wrapper code, only process instantiated functions
      return;
    }

  DUMP("CC pass on: %s\n", function_name(fn));

  stack.reserve(16);
  bd.resize(n_basic_blocks_for_fn(fn));

  process_block(ENTRY_BLOCK_PTR_FOR_FN(fn), bd, stack);
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
