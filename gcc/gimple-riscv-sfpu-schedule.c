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
#include "config/riscv/sfpu.h"

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

using namespace std;

static void insert_nop(gimple_stmt_iterator gsi)
{
  const riscv_sfpu_insn_data* nop_insnd = riscv_sfpu_get_insn_data(riscv_sfpu_insn_data::sfpnop);
  gcc_assert(nop_insnd != nullptr);
  gimple* nop_stmt = gimple_build_call(nop_insnd->decl, 0);
  update_stmt(nop_stmt);
  gsi_insert_after(&gsi, nop_stmt, GSI_SAME_STMT);
}

// Perform instruction scheduling.  For wormhole this means adding a NOP or
// moving a non-dependent instruction into the single instruction shadow of
// any instruction which uses the MAD unit, which are:	MAD, LUT, LUT32,
// MUL(I), ADD(I).  Note that SWAP/SHFT2 always require a NOP and that is
// emitted along with the instruction and not handled here.
static void transform (function *fn)
{
  DUMP("Schedule pass on: %s\n", function_name(fn));

  bool update = false;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      gimple_stmt_iterator gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gcall *stmt;
	  const riscv_sfpu_insn_data *insnd;
	  if (riscv_sfpu_p(&insnd, &stmt, gsi) &&
	      insnd->schedule == 1)
	    {
	      DUMP("  scheduling %s\n", insnd->name);

	      gcall *next_stmt;
	      const riscv_sfpu_insn_data *next_insnd;
	      if (riscv_sfpu_get_next_sfpu_insn(&next_insnd, &next_stmt, gsi, false))
		{
		  tree lhs = gimple_call_lhs (stmt);
		  use_operand_p use_p;
		  imm_use_iterator iter;
		  bool used = false;
		  if (lhs != nullptr)
		    {
		      FOR_EACH_IMM_USE_FAST (use_p, iter, lhs)
			{
			  gimple *g = USE_STMT(use_p);
			  if (g == next_stmt)
			    {
			      used = true;
			      break;
			    }
			}
		      if (used)
			{
			  DUMP("	next stmt (%s) uses lhs, inserting NOP\n", next_insnd->name);
			  insert_nop(gsi);
			  update = true;
			}
		      else
			{
			  DUMP("	next stmt (%s) is independent, all good\n", next_insnd->name);
			}
		    }
		}
	      else
		{
		  // The stmt needing scheduling is the last stmt in the BB
		  // For now, just add a NOP.  XXXX Could chase down all the next
		  // BBs and possibly do better
		  DUMP(" last stmt in BB, inserting NOP\n");
		  insert_nop(gsi);
		  update = true;
		}
	    }
	  gsi_next (&gsi);
	}
    }

  if (update)
    {
      update_ssa (TODO_update_ssa);
    }
}

namespace {

const pass_data pass_data_riscv_sfpu_schedule =
{
  GIMPLE_PASS, /* type */
  "riscv_sfpu_schedule", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_riscv_sfpu_schedule : public gimple_opt_pass
{
public:
  pass_riscv_sfpu_schedule (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_riscv_sfpu_schedule, ctxt)
  {}

  virtual unsigned int execute (function *);
}; // class pass_riscv_sfpu_schedule

} // anon namespace

/* Entry point to riscv_sfpu_schedule pass.	*/
unsigned int
pass_riscv_sfpu_schedule::execute (function *fun)
{
  if (flag_wormhole)
    {
      transform (fun);
    }

  return 0;
}

gimple_opt_pass *
make_pass_riscv_sfpu_schedule (gcc::context *ctxt)
{
  return new pass_riscv_sfpu_schedule (ctxt);
}
