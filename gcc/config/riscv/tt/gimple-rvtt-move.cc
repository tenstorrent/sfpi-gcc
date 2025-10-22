/* Pass to handle SFPU move operations to respect the CC state
   Copyright (C) 2022-2025 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).

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
#include "tree-eh.h"
#include "gimple-iterator.h"
#include "gimple-fold.h"
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
#include <iostream>
#include <tuple>
#include "rvtt.h"

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
subsequent_use_in_bb(tree var, gimple_stmt_iterator gsi)
{
  use_operand_p use_p;
  imm_use_iterator iter;

  if (!has_zero_uses(var))
    {
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

static bool
subsequent_use(tree var, gimple_stmt_iterator gsi)
{
  gsi_next (&gsi);
  if (subsequent_use_in_bb(var, gsi))
    {
      return true;
    }

  basic_block assign_bb = gimple_bb(SSA_NAME_DEF_STMT(var));

  basic_block bb = gsi_bb(gsi);
  edge_iterator ei;
  edge e;
  FOR_EACH_EDGE(e, ei, bb->succs)
    {
      if (e->dest != assign_bb)
	{
	  gimple_stmt_iterator next_gsi = gsi_start_bb (e->dest);
	  if (!gsi_end_p(next_gsi) &&
	      subsequent_use_in_bb(var, next_gsi))
	    {
	      return true;
	    }
	}
    }

  return false;
}

static inline bool
match_prior_assignment(rvtt_insn_data::insn_id id,
		       const rvtt_insn_data **prior_insnd,
		       gcall **prior_stmt,
		       gimple_stmt_iterator *prior_gsi,
		       tree src)
{
  bool result = false;

  gimple *assign_g = SSA_NAME_DEF_STMT(src);
  if (rvtt_p(prior_insnd, prior_stmt, assign_g)) {
    *prior_gsi = gsi_for_stmt(assign_g);

    result = 
      rvtt_p(prior_insnd, prior_stmt, *prior_gsi) &&
      ((*prior_insnd)->id == id);
  }

  return result;
}

static inline bool
is_const_reg(tree arg)
{
  gimple_stmt_iterator assign_gsi;
  gcall *assign_stmt;
  const rvtt_insn_data *assign_insnd;

  return
    TREE_CODE(arg) == SSA_NAME &&
    match_prior_assignment(rvtt_insn_data::sfpassignlreg,
			   &assign_insnd, &assign_stmt, &assign_gsi, arg) &&
    (get_int_arg(assign_stmt, 0) >= SFPU_USER_REG_NUM);
}

static inline void
insert_move(tree dst_arg, int dst_arg_pos, gcall *stmt, gimple_stmt_iterator gsi)
{
  // Insert a move
  const rvtt_insn_data *mov_insnd = rvtt_get_insn_data(rvtt_insn_data::sfpmov);
  gimple* mov_stmt = gimple_build_call (mov_insnd->decl, 2);
  tree var = create_tmp_var (TREE_TYPE(dst_arg));
  tree name = make_ssa_name (var, mov_stmt);
  gimple_call_set_lhs (mov_stmt, name);
  gimple_call_set_arg(mov_stmt, 0, dst_arg);
  gimple_call_set_arg(mov_stmt, 1, build_int_cst(integer_type_node, 0));
  gsi_insert_before (&gsi, mov_stmt, GSI_SAME_STMT);
  update_stmt(mov_stmt);

  gimple_call_set_arg(stmt, dst_arg_pos, name);
  update_stmt(stmt);

  update_ssa (TODO_update_ssa);
}

// There are 2 kinds of moves that the compiler generates which I call:
// 1) Necessary moves: these occur when an insn is a dst-as-src operation and
//    the src and dst are used later or when the destination register is not
//    writable. Done properly, these operations need to obey the CC state.
//    Sometimes they can be avoided by swapping arguments to the insn.
// 2) Unnecessary moves: sometimes the compiler switches registers for no
//    explicable reason.  Often the old register goes dead and so these can
//    get cleaned up, however, sometimes it is hard to detect what is going on
//    and how it can get cleaned up.  It is hard to create test cases to find
//    these, in all cases to date (6/16/22) the CC state must be respected.
//
// If #1 was handled by the compiler, then it would be hard/impossible to
// determine whether the move should obey the CC state.  So...these are
// handled in this pass.
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
	  const rvtt_insn_data *candidate_insnd;

	  if (rvtt_p(&candidate_insnd, &candidate_stmt, candidate_gsi) &&
	      candidate_insnd->id != rvtt_insn_data::nonsfpu &&
	      candidate_insnd->dst_as_src_p())
	    {
	      DUMP("Processing %s\n", candidate_insnd->name);

	      // Note: all dst_arg_as_src insns' src_arg_pos immediately follows dst_arg_pos (hack)
	      tree src_arg = gimple_call_arg(candidate_stmt, candidate_insnd->dst_arg_pos + 1);
	      tree dst_arg = gimple_call_arg(candidate_stmt, candidate_insnd->dst_arg_pos);

	      bool permutable = rvtt_permutable_operands(candidate_insnd, candidate_stmt);
	      bool src_is_const_reg = permutable && is_const_reg(src_arg);
	      bool dst_is_const_reg = is_const_reg(dst_arg);

	      if (dst_is_const_reg || subsequent_use(dst_arg, candidate_gsi))
		{
		  // Try to swap operands to eliminate the need for a move
		  if (!src_is_const_reg &&
		      permutable &&
		      !subsequent_use(src_arg, candidate_gsi))
		    {
		      DUMP("  swapping arguments\n");

		      gimple_call_set_arg(candidate_stmt, candidate_insnd->dst_arg_pos, src_arg);
		      gimple_call_set_arg(candidate_stmt, candidate_insnd->dst_arg_pos + 1, dst_arg);
		      update_stmt(candidate_stmt);
		    }
		  else
		    {
		      DUMP("  inserting move\n");
		      
		      insert_move(dst_arg, candidate_insnd->dst_arg_pos, candidate_stmt, candidate_gsi);
		    }
		}
	    }

	  gsi_next (&candidate_gsi);
	}
    }
}

namespace {

const pass_data pass_data_rvtt_move =
{
  GIMPLE_PASS, /* type */
  "rvtt_move", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_move : public gimple_opt_pass
{
public:
  pass_rvtt_move (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_move, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_RVTT;
  }

  virtual unsigned execute (function *fn) override
  {
    transform (fn);
    return 0;
  }
}; // class pass_rvtt_move

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_move (gcc::context *ctxt)
{
  return new pass_rvtt_move (ctxt);
}
