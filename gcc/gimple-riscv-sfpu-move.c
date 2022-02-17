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

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

using namespace std;

static long int
get_int_arg(gcall *stmt, unsigned int arg)
{
  tree decl = gimple_call_arg(stmt, arg);
  if (decl)
  {
    gcc_assert(TREE_CODE(decl) == INTEGER_CST);
    return *(decl->int_cst.val);
  }
  return -1;
}

static bool
subsequent_use(tree var, gimple_stmt_iterator gsi)
{
  use_operand_p use_p;
  imm_use_iterator iter;

  if (!has_zero_uses(var))
    {
      gsi_next (&gsi);
      while (!gsi_end_p (gsi))
        {
          gimple *g = gsi_stmt (gsi);

          if (g->code != GIMPLE_DEBUG)
            {
              FOR_EACH_IMM_USE_FAST (use_p, iter, var)
                {
                  if (g == USE_STMT(use_p))
                    {
                        return true;
                    }
                }
            }

          gsi_next (&gsi);
        }
    }

  return false;
}

static inline bool
match_prior_assignment(riscv_sfpu_insn_data::insn_id id,
		       const riscv_sfpu_insn_data **prior_insnd,
		       gcall **prior_stmt,
		       gimple_stmt_iterator *prior_gsi,
		       tree src)
{
  gimple *assign_g = SSA_NAME_DEF_STMT(src);
  riscv_sfpu_p(prior_insnd, prior_stmt, assign_g);
  *prior_gsi = gsi_for_stmt(assign_g);
  return
    riscv_sfpu_p(prior_insnd, prior_stmt, *prior_gsi) &&
    ((*prior_insnd)->id == id);
}

static inline void
insert_move(tree dst_arg, int dst_arg_pos, gcall *stmt, gimple_stmt_iterator gsi)
{
  DUMP("  inserting move\n");

  // Insert a move
  const riscv_sfpu_insn_data *mov_insnd = riscv_sfpu_get_insn_data("__builtin_riscv_sfpmov");
  gimple* mov_stmt = gimple_build_call (mov_insnd->decl, 2);
  tree var = create_tmp_var (TREE_TYPE(dst_arg));
  tree name = make_ssa_name (var, mov_stmt);
  gimple_call_set_lhs (mov_stmt, name);
  gimple_call_set_arg(mov_stmt, 0, dst_arg);
  gimple_call_set_arg(mov_stmt, 1, build_int_cst(integer_type_node, 0));
  gsi_insert_before (&gsi, mov_stmt, GSI_SAME_STMT);

  gimple_call_set_arg(stmt, dst_arg_pos, name);
  update_ssa (TODO_update_ssa);
}

// On Grayskull, the mov instruction is not aware of the CC state
// This means a compiler generated move consists of pushc/encc/mov/popc (plus nops).
// This pass avoids moves when possible (by swapping operands) and injects
// simple moves when necessary to avoid the later compiler generated movs
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

	  if (riscv_sfpu_p(&candidate_insnd, &candidate_stmt, candidate_gsi) &&
              candidate_insnd->id != riscv_sfpu_insn_data::nonsfpu &&
	      candidate_insnd->uses_dst_as_src())
	    {
	      tree dst_arg = gimple_call_arg(candidate_stmt, candidate_insnd->dst_arg_pos);

	      if (subsequent_use(dst_arg, candidate_gsi))
		{
		  DUMP("Processing %s\n", candidate_insnd->name);

		  bool swapped = false;

		  // Try to swap operands to eliminate the need for a move
		  if (riscv_sfpu_permutable_operands(candidate_insnd, candidate_stmt))
		    {
		      // Note: all dst_arg_as_src insns' src_arg_pos immediately follows dst_arg_pos
		      tree src_arg = gimple_call_arg(candidate_stmt, candidate_insnd->dst_arg_pos + 1);

		      if (!subsequent_use(src_arg, candidate_gsi)) {
			DUMP("  swapping arguments\n");

			// Swap args
			gimple_call_set_arg(candidate_stmt, candidate_insnd->dst_arg_pos, src_arg);
			gimple_call_set_arg(candidate_stmt, candidate_insnd->dst_arg_pos + 1, dst_arg);
			swapped = true;
		      }
		    }

		  if (!swapped)
		    {
		      insert_move(dst_arg, candidate_insnd->dst_arg_pos, candidate_stmt, candidate_gsi);
		    }
		}
	      else
		{
		  gimple_stmt_iterator assign_gsi;
		  gcall *assign_stmt;
		  const riscv_sfpu_insn_data *assign_insnd;
		  if (match_prior_assignment(riscv_sfpu_insn_data::sfpassignlr,
					     &assign_insnd, &assign_stmt, &assign_gsi, dst_arg) &&
		      get_int_arg(assign_stmt, 0) > SFP_LREG_COUNT)
		    {
		      insert_move(dst_arg, candidate_insnd->dst_arg_pos, candidate_stmt, candidate_gsi);
		    }
		}
	    }

	  gsi_next (&candidate_gsi);
	}
    }
}

namespace {

const pass_data pass_data_riscv_sfpu_move =
{
  GIMPLE_PASS, /* type */
  "riscv_sfpu_move", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_riscv_sfpu_move : public gimple_opt_pass
{
public:
  pass_riscv_sfpu_move (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_riscv_sfpu_move, ctxt)
  {}

  virtual unsigned int execute (function *);
}; // class pass_riscv_sfpu_move

} // anon namespace

/* Entry point to riscv_sfpu_move pass.	*/
unsigned int
pass_riscv_sfpu_move::execute (function *fun)
{
  if (flag_sfpu)
    {
      transform (fun);
    }

  return 0;
}

gimple_opt_pass *
make_pass_riscv_sfpu_move (gcc::context *ctxt)
{
  return new pass_riscv_sfpu_move (ctxt);
}
