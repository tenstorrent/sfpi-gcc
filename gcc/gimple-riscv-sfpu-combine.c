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
#include <string>
#include <map>
#include <iostream>
#include <tuple>
#include "config/riscv/sfpu.h"

using namespace std;

static long int
get_int_arg(gcall *stmt, unsigned int arg)
{
  tree decl = gimple_call_arg(stmt, arg);
  if (decl)
  {
    return *(decl->int_cst.val);
  }
  return -1;
}

// Starting from gsi, find the stmt using cond_src as the LHS, return in prior_gsi
static bool
find_prior_assignment(gimple_stmt_iterator *prior_gsi, gimple_stmt_iterator gsi, tree cond_src)
{
    gimple_stmt_iterator start = gsi_start_bb(gsi_bb(gsi));

    gsi_prev (&gsi);
    while (!gsi_end_p(gsi) && gsi_stmt(gsi) != gsi_stmt(start))
      {
	gimple *g = gsi_stmt (gsi);

	if (g->code == GIMPLE_CALL)
	  {
	    gcall *stmt = dyn_cast<gcall *> (g);
	    tree lhs = gimple_call_lhs (stmt);

	    if (lhs == cond_src) {
		*prior_gsi = gsi;
		return true;
	    }
	  }

	gsi_prev (&gsi);
      }

    return false;
}

// Return whether the call/stmt can be combined with an iadd_i
static bool
can_combine_iadd_i(const riscv_sfpu_insn_data *insnd, gcall *stmt)
{
  return
    (insnd->id == riscv_sfpu_insn_data::sfpiadd_i && get_int_arg(stmt, 3) == 5) ||
    (insnd->id == riscv_sfpu_insn_data::sfpiadd_i_lv && get_int_arg(stmt, 4) == 5) ||
    (insnd->id == riscv_sfpu_insn_data::sfpiadd_v &&
     (get_int_arg(stmt, 2) == 4 || get_int_arg(stmt, 2) == 6)) ||
    (insnd->id == riscv_sfpu_insn_data::sfpexexp && get_int_arg(stmt, 1) == 0) ||
    (insnd->id == riscv_sfpu_insn_data::sfpexexp_lv && (get_int_arg(stmt, 2) == 0));
}

// Combine stmt (an iadd_i) with candidate_stmt by updating mod1/im of
// candidate_stmt
static void
combine_iadd_i(const riscv_sfpu_insn_data *insnd, gcall *stmt, gcall *candidate_stmt)
{
  int candidate_mod1 = get_int_arg(candidate_stmt, 3);

  switch (insnd->id) {
  case riscv_sfpu_insn_data::sfpiadd_i:
    // Use mod1, imm from candidate
    gimple_call_set_arg(stmt, 3, gimple_call_arg(candidate_stmt, 3));
    break;
  case riscv_sfpu_insn_data::sfpiadd_i_lv:
      // Use mod1, imm from candidate
      gimple_call_set_arg(stmt, 4, gimple_call_arg(candidate_stmt, 3));
    break;
  case riscv_sfpu_insn_data::sfpiadd_v:
    {
      int assign_mod1 = get_int_arg(stmt, 2);
      int new_mod1 = (candidate_mod1 & ~1) | (assign_mod1 & ~4);

      // Use mod1
      gimple_call_set_arg(stmt, 2, build_int_cst(integer_type_node, new_mod1));
    }
    break;
  case riscv_sfpu_insn_data::sfpexexp:
    // candidate_mod1 is 1 or 9, which map to 2 and 10 for exexp
    gimple_call_set_arg(stmt, 1, build_int_cst(integer_type_node, candidate_mod1 + 1));
    break;
  case riscv_sfpu_insn_data::sfpexexp_lv:
    // candidate_mod1 is 1 or 9, which map to 2 and 10 for exexp
    gimple_call_set_arg(stmt, 2, build_int_cst(integer_type_node, candidate_mod1 + 1));
    break;
  default:
    gcc_unreachable();
  }
}

// Returns true iff a stmt between gsi and last is a CC setting stmt
static bool
intervening_cc_stmt(gimple_stmt_iterator gsi, gimple_stmt_iterator last)
{
  gsi_next (&gsi);

  while (gsi.ptr != last.ptr)
  {
    gcall *stmt;
    const riscv_sfpu_insn_data *insnd;
    if (riscv_sfpu_p (&insnd ,&stmt, gsi) && riscv_sfpu_sets_cc(insnd, stmt))
     {
       return true;
     }

    gsi_next (&gsi);
  }

  return false;
}

// Return true iff at least one of the uses of var is between start and end
static bool
intervening_use(tree var, gimple_stmt_iterator start, gimple_stmt_iterator end)
{
  use_operand_p use_p;
  imm_use_iterator iter;

  FOR_EACH_IMM_USE_FAST (use_p, iter, var)
    {
      gimple *g = USE_STMT(use_p);

      // XXXX should the DEBUG stmts get deleted?  Or moved?
      // They will now occur above the assignment
      if (g->code != GIMPLE_DEBUG)
	{
	  gimple_stmt_iterator gsi = start;
	  while (gsi.ptr != end.ptr)
	    {
	      if (g == gsi_stmt (gsi))
		{
		  return true;
		}
	      gsi_next (&gsi);
	    }
	}
    }

  return false;
}

// Optimize stmt sequence by combining builtins.  Handles:
//  - iadd_i when used to set the CC but w/ no LHS combines with other CC
//    stmts which don't set the CC, e.g., iadd_i, iadd_v, exexp
//  - setcc/lz are not optimized here.	the usage pattern of that combination
//    is unlikely to show up much in real life and some cases are presentlruny
//    handled by a peephole optimization.  Move it here if ever worthwhile
//
// Works by looking within a BB:
//  - find candidate add_i which sets the CC and compares against 0
//  - only combine if there are no subsequent uses of the LHS (null LHS)
//  - find the assignment of the variable used as src in the add_i
//  - ensure there are no CC stmts in between assignment and use
//  - ensure there are no other uses between assignment and use
//  - update assignment mod1 value to set the CC/imm as relevant 
//  - move the assignment stmt to the location of the candidate stmt
//  - delete the candidate iadd_i
static void transform (function *fun)
{
  basic_block bb;
  gimple_stmt_iterator candidate_gsi;

  // Find all function calls
  FOR_EACH_BB_FN (bb, fun)
    {
      candidate_gsi = gsi_start_bb (bb);
      while (!gsi_end_p (candidate_gsi))
	{
	  gcall *candidate_stmt;
	  const riscv_sfpu_insn_data *candidate_insnd;

	  if (riscv_sfpu_p(&candidate_insnd, &candidate_stmt, candidate_gsi))
	    {
	      // Check for candidate iadd_i that sets the CC and compares to 0
	      if (candidate_insnd->id == riscv_sfpu_insn_data::sfpiadd_i &&
		  (get_int_arg(candidate_stmt, 2) == 0) &&
		  (get_int_arg(candidate_stmt, 3) == 1 || get_int_arg(candidate_stmt, 3) == 9) &&
		  gimple_call_lhs(candidate_stmt) == nullptr)
		{
		  // Got a candidate
		  gimple_stmt_iterator assign_gsi;

		  // Find when this variable was assigned
		  if (find_prior_assignment(&assign_gsi, candidate_gsi,
					    gimple_call_arg(candidate_stmt, 1)))
		    {
		      gcall *assign_stmt;
		      const riscv_sfpu_insn_data *assign_insd;
		      riscv_sfpu_p(&assign_insd, &assign_stmt, assign_gsi);

		      if (!intervening_cc_stmt(assign_gsi, candidate_gsi) &&
			  !intervening_use(gimple_call_lhs(assign_stmt), assign_gsi, candidate_gsi))
			{
			  // Check to see if the assignment is one of the targeted optimizations
			  if (can_combine_iadd_i(assign_insd, assign_stmt))
			    {
			      // Found a replaceable iadd_i
			      combine_iadd_i(assign_insd, assign_stmt, candidate_stmt);

			      // Move target
			      gsi_move_before(&assign_gsi, &candidate_gsi);

			      // Remove candidate
			      release_defs(candidate_stmt);
			      unlink_stmt_vdef(candidate_stmt);
			      gsi_remove(&candidate_gsi, true);
			    }
			}
		    }
		}
	    }
	  gsi_next (&candidate_gsi);
	}
    }
}

namespace {

const pass_data pass_data_riscv_sfpu_combine =
{
  GIMPLE_PASS, /* type */
  "riscv_sfpu_combine", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_riscv_sfpu_combine : public gimple_opt_pass
{
public:
  pass_riscv_sfpu_combine (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_riscv_sfpu_combine, ctxt)
  {}

  virtual unsigned int execute (function *);
}; // class pass_riscv_sfpu_combine

} // anon namespace

/* Entry point to riscv_sfpu_combine pass.	*/
unsigned int
pass_riscv_sfpu_combine::execute (function *fun)
{
  transform (fun);
  return 0;
}

gimple_opt_pass *
make_pass_riscv_sfpu_combine (gcc::context *ctxt)
{
  return new pass_riscv_sfpu_combine (ctxt);
}
