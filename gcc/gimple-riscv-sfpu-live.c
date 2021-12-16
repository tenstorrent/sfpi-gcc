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

//////////////////////////////////////////////////////////////////////////////
// block_data: structure used when processing blocks to track:
//   BD_VisitedP: whether or not a block has been visited previously
//   BD_CCLevel: the CC level at entry to the block on the first visit
//   BD_Live: whether all variable uses in the block should be treated as live,
//         this occurs when the block is entered multiple times with different
//         nest levels
//   BD_StackDepth: the CC stack depth at entry, used for sanity checks
typedef vector<tuple<bool, unsigned int, bool, unsigned int>> block_data;

enum {
  BD_VisitedP = 0,
  BD_CCLevel = 1,
  BD_Live = 2,
  BD_StackDepth = 3,
};

//////////////////////////////////////////////////////////////////////////////
// Track "liveness"
// Map statements to:
//   level: The depth of CC setting stmts that were hit prior to this
//          statement
//   generation: The CC "generation" of the statement.  A new generation
//               occurs whenever a CC setting statement is after the LV_CCLevel
//               has decreased (POPC) or for the first generation...
//   force: For the statement to be live (happens when a BB is hit mulitple
//          times w/ different levels
static const int liveness_undefined = 0xFFFF;
struct liveness_data {
  unsigned int level;
  unsigned int generation;
  bool force;

  liveness_data(unsigned int l, unsigned int g, bool f) : level(l), generation(g), force(f) {}
};

typedef map<gcall *, struct liveness_data> call_liveness;


//////////////////////////////////////////////////////////////////////////////
static bool
process_block_stmts(basic_block bb,
		    liveness_data *current,
		    bool *cascading,
		    unsigned int *gen_count,
		    vector<liveness_data> &stack,
		    call_liveness &liveness)
{
  gimple_stmt_iterator gsi;
  bool found_sfpu = false;

  DUMP("    process_block_stmts CC %d gen %d depth %zu\n",
	 current->level, current->generation, stack.size());

  gsi = gsi_start_bb (bb);
  while (!gsi_end_p (gsi))
    {
      gcall *stmt;
      const riscv_sfpu_insn_data *insnd;
      if (riscv_sfpu_p(&insnd, &stmt, gsi))
	{
	  if (insnd->id != riscv_sfpu_insn_data::nonsfpu)
	    {
	      found_sfpu = true;
	      if (insnd->id == riscv_sfpu_insn_data::sfppushc)
		{
		  if (!*cascading)
		    {
		      current->generation = *gen_count;
		      (*gen_count)++;
		      *cascading = true;
		    }
		  stack.push_back(*current);
		}
	      else if (insnd->id == riscv_sfpu_insn_data::sfppopc)
		{
		  if (stack.size() == 0)
		    {
		      error("Error: malformed program, popc without matching pushc - exiting!");
		    }
		  *current = stack.back();
		  *cascading = false;
		  stack.pop_back();
		}
	      else
		{
		  gcc_assert(liveness.find(stmt) == liveness.end());
		  liveness.insert(pair<gcall *, liveness_data>(stmt, *current));
		  if (riscv_sfpu_sets_cc(insnd, stmt) &&
		      insnd->id != riscv_sfpu_insn_data::sfpencc)
		    {
		      if (stack.size() == 0)
			{
			  error("Error: malformed program, %s outside of pushc/popc - exiting!", insnd->name);
			}
		      current->level++;
		    }
		}
	    }
	}

      gsi_next (&gsi);
    }

  return found_sfpu;
}

// Process a block
static bool
process_block(basic_block bb,
	      block_data& bd,
	      liveness_data current,
	      bool cascading,
	      unsigned int gen_count,
	      vector<liveness_data> stack,
	      call_liveness &liveness)
{
  edge_iterator ei;
  edge e;

  DUMP("    process_block bb %d CC %d gen %d depth %zu\n",
       bb->index, current.level, current.generation, stack.size());

  if (get<BD_VisitedP>(bd[bb->index]))
    {
      gcc_assert(stack.size() == get<BD_StackDepth>(bd[bb->index]));

      // If we hit a BB and the CC level isn't the same as another hit, then
      // everything must be live
      if (!get<BD_Live>(bd[bb->index]) &&
	  current.level > get<BD_CCLevel>(bd[bb->index]))
	{
	  DUMP("     re-visit block w/ higher CC, marking all live\n");

	  // This bb has been visited and the current CC level is greater than
	  // previous, so mark all assignments within for variables defined
	  // without as live
	  get<BD_Live>(bd[bb->index]) = true;

	  gimple_stmt_iterator gsi;
	  gsi = gsi_start_bb (bb);
	  while (!gsi_end_p (gsi))
	    {
	      gcall *stmt;
	      const riscv_sfpu_insn_data *insnd;
	      if (riscv_sfpu_p(&insnd, &stmt, gsi))
		{
		  auto loc = liveness.find(stmt);
		  if (loc != liveness.end())
		    {
		      loc->second.force = true;
		    }
		}
	      gsi_next (&gsi);
	    }
	}

      return false;
    }

  get<BD_VisitedP>(bd[bb->index]) = true;
  get<BD_CCLevel>(bd[bb->index]) = current.level;
  get<BD_StackDepth>(bd[bb->index]) = stack.size();

  bool found_sfpu = process_block_stmts(bb, &current, &cascading, &gen_count, stack, liveness);

  // When we leave, EDGE_COUNT == 0, stack must be empty
  gcc_assert(EDGE_COUNT(bb->succs) != 0 || stack.size() == 0);

  FOR_EACH_EDGE(e, ei, bb->succs)
    {
      found_sfpu = process_block(e->dest, bd, current, cascading, gen_count, stack, liveness) || found_sfpu;
    }

  return found_sfpu;
}

static unsigned int
find_live_arg(gcall *stmt)
{
  // First arg is the insn_buffer pointer, if it is present
  // Next arg (so first or second) is the live arg
  unsigned int live_arg;
  unsigned num_of_ops = gimple_call_num_args (stmt);
  gcc_assert(num_of_ops >= 1);
  if (POINTER_TYPE_P (TREE_TYPE (gimple_call_arg(stmt, 0))))
    {
      gcc_assert(num_of_ops >= 2);
      live_arg = 1;
    }
  else
    {
      live_arg = 0;
    }

  return live_arg;
}

// Pointer chase back to the root if needed
//
// PHI nodes occur where multiple BB come together
// We are interested in the single defining stmt of an SSA var and what the
// liveness is at that time
// We don't care about all the paths, just one path back to the definition
// However, if we hit a loop, we need to move on to the next path
static liveness_data
get_def_stmt_liveness_1(function *fn, vector<bool> &visited, liveness_data data,
			gimple *def_g, const call_liveness& liveness)
{
  if (def_g == nullptr)
    {
      DUMP("      null gimple, unassigned var\n");
      data.level = liveness_undefined;
      return data;
    }

  if (def_g->code == GIMPLE_PHI)
    {
      DUMP("      process phi\n");

      int n = gimple_phi_num_args (def_g);

      /* If we see non zero constant, we should punt.  The predicate
       * should be one guarding the phi edge.  */
      for (int i = 0; i < n; ++i)
	{
	  tree op = gimple_phi_arg_def (def_g, i);
	  edge e = gimple_phi_arg_edge (dyn_cast<gphi *>(def_g), i);
	  basic_block bb = e->src;
	  if (!visited[bb->index])
	    {
	      visited[bb->index] = true;
	      get_def_stmt_liveness_1(fn, visited, data, SSA_NAME_DEF_STMT(op), liveness);
	    }
	}
    }
  else
    {
      gcall *stmt = dyn_cast<gcall *>(def_g);

      if (stmt == nullptr)
	{
	  DUMP("      null statement, unassigned var\n");
	  data.level = liveness_undefined;
	}
      else
	{
	  const riscv_sfpu_insn_data * insnd = riscv_sfpu_get_insn_data(stmt);
	  if (insnd && insnd->id != riscv_sfpu_insn_data::nonsfpu && insnd->live)
	    {
	      DUMP("      chase assignment\n");
	      // If the defining statement is another _lv insn, chase it through
	      unsigned int live_arg = find_live_arg (stmt);
	      data = get_def_stmt_liveness_1(fn, visited, data,
					     SSA_NAME_DEF_STMT(gimple_call_arg(stmt, live_arg)), liveness);
	    }
	  else
	    {
	      // The chain ends at a non-live insn, use its liveness as the result
	      data = liveness.find(stmt)->second;
	    }
	}
    }

  return data;
}

// Find the single root of the assignment chain, across BBs
static liveness_data
get_def_stmt_liveness(function *fn, gcall *stmt, const call_liveness& liveness)
{
  liveness_data data(0, 0, false);
  vector<bool> visited;

  visited.resize(n_basic_blocks_for_fn(fn));

  visited[gimple_bb(stmt)->index] = true;

  unsigned int live_arg = find_live_arg (stmt);
  gimple *def_g = SSA_NAME_DEF_STMT (gimple_call_arg(stmt, live_arg));

  return get_def_stmt_liveness_1(fn, visited, data, def_g, liveness);
}

// If the argument in arg has one use, then that use is in a stmt that is about
// to be deleted.  Set the defining statement to produce NULL and release the defs
static void
cleanup_arg_ssa(tree arg)
{
  if (num_imm_uses (arg) == 1)
    {
      gimple *def_g = SSA_NAME_DEF_STMT (arg);

      if (def_g->code == GIMPLE_PHI)
	{
	  // XXXX handle phi
	  // this seems to work fine and SSA checks are ok w/ doing nothing
	  DUMP("    cleanup_arg_ssa do nothing for phi\n");
	}
      else
	{
	  DUMP("    cleanup_arg_ssa handling call\n");

	  gimple_call_set_lhs(def_g, NULL_TREE);
	  tree lhs_name = gimple_call_lhs (def_g);
	  release_ssa_name(lhs_name);
	  update_stmt (def_g);
	}
    }
}

static void
copy_args_from_live_insn (gimple *new_stmt, unsigned int live_arg, gcall *stmt)
{
  // Copy initial args
  for (unsigned int i = 0; i < live_arg; i++)
    {
      gimple_call_set_arg (new_stmt, i, gimple_call_arg (stmt, i));
    }

  // Shift remaining arguments by 1
  for (unsigned int i = live_arg; i < gimple_call_num_args (stmt); i++)
    {
      gimple_call_set_arg (new_stmt, i, gimple_call_arg (stmt, i + 1));
    }
}

static void
copy_args_to_live_insn (gimple *new_stmt, unsigned int live_arg, gcall *stmt)
{
  // Copy initial args
  for (unsigned int i = 0; i < live_arg; i++)
    {
      gimple_call_set_arg (new_stmt, i, gimple_call_arg (stmt, i));
    }

  // Shift remaining arguments by 1
  for (unsigned int i = live_arg; i < gimple_call_num_args (stmt); i++)
    {
      gimple_call_set_arg (new_stmt, i + 1, gimple_call_arg (stmt, i));
    }
}

// break_liveness
// Processes the liveness data to find instructions that do not need to be
// live and breaks the liveness connection by either deleting the sfpassign_lv
// (livness assignment) or changing the instruction to the non-live version
static void
break_liveness(function *fn, call_liveness liveness)
{
  DUMP("  Break liveness\n");
  call_liveness::iterator it;

  // Iterate over all the sfpu instructions
  for (it = liveness.begin(); it != liveness.end(); it++)
    {
      gcall *stmt = it->first;
      const riscv_sfpu_insn_data *insnd = riscv_sfpu_get_insn_data(stmt);

      if (insnd->id != riscv_sfpu_insn_data::nonsfpu && insnd->live)
	{
	  // Find which position the "live" arg is in
	  unsigned int live_arg = find_live_arg (stmt);

	  // Get the defining statement and it's cc_count for this SSA
	  liveness_data cur_stmt_liveness = it->second;
	  liveness_data def_stmt_liveness = get_def_stmt_liveness(fn, stmt, liveness);

	  DUMP("    liveness: cur %d def %d force %d\n",
		       cur_stmt_liveness.level, def_stmt_liveness.level, cur_stmt_liveness.force);

	  if (def_stmt_liveness.level == liveness_undefined)
	    {
	      continue;
	    }

	  if ((cur_stmt_liveness.level <= def_stmt_liveness.level &&
	       cur_stmt_liveness.generation <= def_stmt_liveness.generation) ||
	      cur_stmt_liveness.force)
	    {
	      if (insnd->id == riscv_sfpu_insn_data::sfpassign_lv)
		{
		  DUMP("    removing sfpassign\n");
		  tree lhs = gimple_call_lhs (stmt);
		  if (lhs != nullptr)
		    {
		      // For assignments, find all occurences of the LHS,
		      // replace w/ the live SSA

		      tree live_ssa = gimple_call_arg (stmt, 1);
		      gimple * use_stmt;
		      imm_use_iterator iter;
		      FOR_EACH_IMM_USE_STMT (use_stmt, iter, lhs)
			{
			  if (is_gimple_debug (use_stmt))
			    continue;
			  if (use_stmt != stmt)
			    {
			      if (use_stmt->code == GIMPLE_PHI)
				{
				  for (unsigned int i = 0; i < gimple_phi_num_args (use_stmt); i++)
				    {
				      if (gimple_phi_arg_def (use_stmt, i) == lhs)
					{
					  SET_PHI_ARG_DEF(as_a <gphi *> (use_stmt), i, live_ssa);
					  update_stmt (use_stmt);
					  break;
					}
				    }
				}
			      else
				{
				  for (unsigned int i = 0; i < gimple_call_num_args (use_stmt); i++)
				    {
				      if (gimple_call_arg (use_stmt, i) == lhs)
					{
					  gimple_call_set_arg(use_stmt, i, live_ssa);
					  update_stmt (use_stmt);
					}
				    }
				}
			    }
			}
		    }

		  // Remove assign
		  gimple_stmt_iterator gsi = gsi_for_stmt(stmt);

		  // Update SSA for the 2 arguments of the assign
		  gcc_assert(gimple_call_num_args(stmt) == 2);
		  cleanup_arg_ssa (gimple_call_arg(stmt, 0));
		  cleanup_arg_ssa (gimple_call_arg(stmt, 1));

		  unlink_stmt_vdef(stmt);
		  gsi_remove(&gsi, true);
		  release_defs(stmt);
		}
	      else
		{
		  // For all other instructions replace insn w/ non-live
		  // version and remove the "live" variable
		  const riscv_sfpu_insn_data *new_insnd = riscv_sfpu_get_notlive_version(insnd);
		  DUMP("    replacing %s with %s\n", insnd->name, new_insnd->name);

		  gimple* new_stmt = gimple_build_call (new_insnd->decl, gimple_call_num_args(stmt) - 1);
		  gcc_assert(new_stmt != nullptr);
		  gimple_call_set_lhs (new_stmt, gimple_call_lhs (stmt));

		  copy_args_from_live_insn (new_stmt, live_arg, stmt);

		  gimple_stmt_iterator psi = gsi_for_stmt (stmt);
		  move_ssa_defining_stmt_for_defs (new_stmt, stmt);
		  gimple_set_vuse (new_stmt, gimple_vuse (stmt));
		  gimple_set_vdef (new_stmt, gimple_vdef (stmt));
		  gimple_set_location (new_stmt, gimple_location (stmt));
		  if (gimple_block (new_stmt) == NULL_TREE)
		    {
		      gimple_set_block (new_stmt, gimple_block (stmt));
		    }

		  gsi_replace (&psi, new_stmt, false);
		  update_stmt (new_stmt);
		}
	    }
	}
    }
}

// fold_live_assign
// Removes sfpassign_lv instructions by folding:
//    ssa1 = __builtin_riscv_sfp<foo>(...)
//    ssa2 = __builtin_riscv_sfpassign_lv(ssa_live, ssa1)
// into:
//    ssa2 = __builtin_riscv_sfp<foo>_lv(..., ssa_live, ...)
static void
fold_live_assign (function *fn)
{
  basic_block bb;
  gimple_stmt_iterator gsi;

  DUMP("  Fold live\n");

  FOR_EACH_BB_FN (bb, fn) {
    gsi = gsi_start_bb (bb);
    while (!gsi_end_p (gsi))
      {
	gcall *stmt;
	const riscv_sfpu_insn_data *insnd;
	if (riscv_sfpu_p (&insnd ,&stmt, gsi) &&
	    insnd->id == riscv_sfpu_insn_data::sfpassign_lv)
	  {
	    tree lhs = gimple_call_lhs (stmt);
	    tree live_arg = gimple_call_arg (stmt, 0);

	    gimple_stmt_iterator prev_gsi = gsi;
	    gsi_prev_nondebug (&prev_gsi);
	    gcall *prev_stmt;
	    const riscv_sfpu_insn_data *prev_insnd;
	    if (riscv_sfpu_p (&prev_insnd, &prev_stmt, prev_gsi) &&
		prev_insnd->id != riscv_sfpu_insn_data::nonsfpu)
	      {
		DUMP("    folding %s\n", prev_insnd->name);

		const riscv_sfpu_insn_data *new_insnd = riscv_sfpu_get_live_version(prev_insnd);
		if (new_insnd != nullptr)
		  {
		    // Create _lv version of prev, delete assign stmt, delete prev_stmt
		    DUMP("    building new %s\n", new_insnd->name);
		    gimple *new_stmt = gimple_build_call (new_insnd->decl,
							  gimple_call_num_args (prev_stmt) + 1);
		    gimple_call_set_lhs (new_stmt, lhs);

		    // Find which position the "live" arg is in
		    unsigned int live_arg_num = find_live_arg (prev_stmt);
		    copy_args_to_live_insn (new_stmt, live_arg_num, prev_stmt);

		    // Copy the live arg
		    gimple_call_set_arg (new_stmt, live_arg_num, live_arg);

		    // XXXX THIS vuse doesn't include the use of the live arg!!!!
		    // Need to create new vuse based on prev_stmt but add in
		    // the live arg then set that here
		    gimple_set_vuse (new_stmt, gimple_vuse (prev_stmt));

		    gimple_set_location (new_stmt, gimple_location (prev_stmt));
		    gimple_set_modified (new_stmt, true);
		    gsi_insert_before(&prev_gsi, new_stmt, GSI_SAME_STMT);

		    unlink_stmt_vdef(prev_stmt);
		    gsi_remove(&prev_gsi, true);
		    release_defs(prev_stmt);

		    // Remove candidate
		    gsi_remove(&gsi, true);

		    update_ssa (TODO_update_ssa);
		    continue;
		  }
		else if (prev_insnd->pseudo_live)
		  {
		    // Copy the pseudo-live arg
		    // XXXX
		    // Optimization potential, not clear it is attainable with
		    // the information present.  Consider cases, eg:
		    // a = a - b;
		    // b = a - b;
		    // c = a - b;
		    // Where all of a, b, c need to be preserved
		    // I believe these could all be handled by a compiler
		    // generated move that respects the CC (vs a move of the
		    // whole register), except the compiler thinks that after
		    // the move the 2 registers are interchangeable, which
		    // they are not.  So, present solution looks like:
		    //    move whole register
		    //    subtract
		    //    live move result into place
		    // Yikes
#if 0
		    gimple_call_set_lhs (prev_stmt, lhs);
		    if (lhs != nullptr)
		      {
			gimple_set_vdef (prev_stmt, gimple_vdef (stmt));
			SSA_NAME_DEF_STMT (lhs) = prev_stmt;
		      }

		    gimple_set_modified (prev_stmt, true);
		    unlink_stmt_vdef(prev_stmt);
		    gsi_remove(&gsi, true);
		    release_defs(prev_stmt);
		    continue;
#endif
		  }
	      }
	  }

	gsi_next (&gsi);
      }
  }
}

// Liveness BB analysis
// This pass has to occur before SSA form is generated since that breaks the
// association between instructions that should remain live.  However, at this
// point in the pipeline, the following still exist:
//  - lots of already inlined function bodies
//  - POPC loops are not unrolled
static void
transform (function *fn)
{
  block_data bd;
  call_liveness liveness;
  vector<liveness_data> stack;

  DUMP("Liveness pass on: %s\n", function_name(fn));

  if (lookup_attribute ("always_inline", DECL_ATTRIBUTES (fn->decl)) != NULL)
    {
      // Skip the wrapper code, only process instantiated functions
      return;
    }

  stack.reserve(16);
  bd.resize(n_basic_blocks_for_fn(fn));

  if (process_block(ENTRY_BLOCK_PTR_FOR_FN(fn), bd, liveness_data(0, 0, false), true, 0, stack, liveness))
    {
      break_liveness(fn, liveness);
      fold_live_assign(fn);
    }
}

namespace {

const pass_data pass_data_riscv_sfpu_live =
{
  GIMPLE_PASS, /* type */
  "riscv_sfpu_live", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_riscv_sfpu_live : public gimple_opt_pass
{
public:
  pass_riscv_sfpu_live (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_riscv_sfpu_live, ctxt)
  {}

  virtual unsigned int execute (function *);
}; // class pass_riscv_sfpu_live

} // anon namespace

/* Entry point to riscv_sfpu_live pass.	*/
unsigned int
pass_riscv_sfpu_live::execute (function *fun)
{
  transform (fun);
  return 0;
}

gimple_opt_pass *
make_pass_riscv_sfpu_live (gcc::context *ctxt)
{
  return new pass_riscv_sfpu_live (ctxt);
}
