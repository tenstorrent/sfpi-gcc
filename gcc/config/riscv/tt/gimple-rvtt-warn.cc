/* Pass to issue warnings for SFPU operations
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
#include <unordered_set>
#include "rvtt.h"

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

using namespace std;

namespace {

static bool
check_function_decl(function *fun)
{
  bool bad = false;
  tree arg = DECL_ARGUMENTS (fun->decl);

  while (arg)
    {
      tree type = TREE_TYPE(arg);
      if (TREE_CODE(type) == RECORD_TYPE &&
	  TYPE_NAME(type) != NULL_TREE && TREE_CODE(TYPE_NAME(type)) == TYPE_DECL &&
	  TREE_CODE(DECL_NAME(TYPE_NAME(type))) == IDENTIFIER_NODE)
	{
	  tree name = DECL_NAME(TYPE_NAME(type));
	  const char* type_name = IDENTIFIER_POINTER(name);
	  if (strcmp(type_name, "vFloat") == 0 ||
	      strcmp(type_name, "vInt") == 0 ||
	      strcmp(type_name, "vUInt") == 0)
	    {
	      DUMP("  fun decl w/ %s\n", type_name);
	      bad = true;
	    }
	}
      arg = DECL_CHAIN (arg);
    }

  return bad;
}

static void
warn_replace_stmt(gimple_stmt_iterator *gsi, tree lhs, location_t loc)
{
  DUMP("  replacing stmt\n");
  const rvtt_insn_data* loadi_insnd = rvtt_get_insn_data(rvtt_insn_data::sfpxloadi);

  gimple *loadi_use_stmt = gimple_build_call (loadi_insnd->decl, 5);
  gimple_call_set_lhs (loadi_use_stmt, lhs);
  gimple_call_set_arg (loadi_use_stmt, 0, build_int_cst_type(build_pointer_type (void_type_node), 0));
  gimple_call_set_arg (loadi_use_stmt, 1, build_int_cst(integer_type_node, 0));
  gimple_call_set_arg (loadi_use_stmt, 2, build_int_cst(integer_type_node, 0));
  gimple_call_set_arg (loadi_use_stmt, 3, build_int_cst(integer_type_node, 0));
  gimple_call_set_arg (loadi_use_stmt, 4, build_int_cst(integer_type_node, 0));

  gimple_set_location (loadi_use_stmt, loc);
  update_stmt(loadi_use_stmt);
  gsi_replace(gsi, loadi_use_stmt, false);
  update_ssa (TODO_update_ssa);
}


// Throws a warning when an SPFU vector is used uninitialized
static void
handle_uninit(function *fun, bool bad_fun_decl, gimple *g, gimple_stmt_iterator *gsi)
{
  tree rhs = gimple_assign_rhs1 (g);
  tree lhs = gimple_assign_lhs (g);

  if (lhs != nullptr &&
      VECTOR_FLOAT_TYPE_P(TREE_TYPE(rhs)))
    {
      gimple * use;
      imm_use_iterator iter;

      FOR_EACH_IMM_USE_STMT (use, iter, lhs)
	{
	  const rvtt_insn_data *insnd;
	  gcall *use_stmt;
	  if (rvtt_p(&insnd, &use_stmt, use))
	    {
	      DUMP(" found an uninitialized vector used later\n");

	      location_t assign_location = gimple_location (g);
	      // Assume any "uninitialized" variables in a function that
	      // wasn't inlined w/ a vec arg are due to the function
	      // declaration.  Once that is fixed, the warnings will show
	      // up in re-compile
	      if (bad_fun_decl)
		{
		  error_at(EXPR_LOCATION(fun->decl),
			   "invalid declaration for function '%s', sfpu types cannot be passed on the stack (missing sfpi_inline?)",
			   function_name(fun));
		}
	      else
		{
                 if (SSA_NAME_VAR(lhs) != nullptr)
                   {
                     warning_at (assign_location, OPT_Wuninitialized,
                                 "%qD is used uninitialized in this function", SSA_NAME_VAR (lhs));
                   }
		}

	      warn_replace_stmt(gsi, lhs, assign_location);
	    }
	}
    }
}

// Recurse through phi nodes, return true if any definition of def is from an
// sfpu statement
static bool
is_sfpu_def(tree def, unordered_set<tree>& visited)
{
  gimple * def_stmt = SSA_NAME_DEF_STMT (def);

  if (def_stmt != nullptr &&
      gimple_code(def_stmt) == GIMPLE_PHI)
    {
      int n = gimple_phi_num_args (def_stmt);
      DUMP(" chasing phi with %d els\n", n);

      for (int i = 0; i < n; ++i)
	{
	  tree newdef = gimple_phi_arg_def (def_stmt, i);

	  if (visited.find(newdef) == visited.end())
	    {
	      visited.insert(newdef);
	      if (newdef != NULL_TREE &&
		  TREE_CODE(newdef) == SSA_NAME &&
		  is_sfpu_def(newdef, visited))
		{
		  return true;
		}
	    }
	}

      return false;
    }
  else
    {
      gcall *stmt;
      const rvtt_insn_data *insnd;

      return rvtt_p (&insnd, &stmt, def_stmt) && !insnd->riscv_p();
    }
}

static void
handle_write(gimple *g)
{
  tree assign_lhs = gimple_assign_lhs (g);
  tree assign_rhs = gimple_assign_rhs1 (g);

  if (assign_lhs != NULL_TREE &&
      TREE_CODE(assign_lhs) == COMPONENT_REF &&
      TREE_CODE(assign_rhs) == SSA_NAME)
    {
      unordered_set<tree> visited;
      visited.insert(assign_rhs);

      if (is_sfpu_def(assign_rhs, visited))
	{
	  DUMP(" writing sfpu to memory\n");

	  location_t location = gimple_nonartificial_location(g);

	  // XXXX the line number is invalid, need to chase down the declaration
	  error_at(location, "cannot write sfpu vector to memory");
	}
    }
}

static void
check_for_mult_div_ops (gimple *g)
{
  if (g->code == GIMPLE_ASSIGN &&
      gimple_assign_rhs_class(g) == GIMPLE_BINARY_RHS)
    {
      tree_code op = gimple_assign_rhs_code (g);
      if (op == MULT_EXPR ||
	  op == TRUNC_DIV_EXPR ||
	  op == CEIL_DIV_EXPR ||
	  op == FLOOR_DIV_EXPR ||
	  op == ROUND_DIV_EXPR ||
	  op == TRUNC_MOD_EXPR ||
	  op == CEIL_MOD_EXPR ||
	  op == FLOOR_MOD_EXPR ||
	  op == ROUND_MOD_EXPR)
	{
	  tree rhs1 = gimple_assign_rhs1(g);
	  tree rhs2 = gimple_assign_rhs2(g);
	  if (TREE_CODE(rhs1) != INTEGER_CST &&
	      TREE_CODE(rhs2) != INTEGER_CST)
	    {
	      if (op == MULT_EXPR)
		{
		  error_at (gimple_nonartificial_location (g), "detected multiply operation");
		}
	      else
		{
		  error_at (gimple_nonartificial_location (g), "detected divide operation");
		}
	    }
	}
    }
}

void check_sfpu(function *fun, gimple_stmt_iterator gsi, gimple *g, bool bad_fun_decl)
{
  gcall *stmt;
  const rvtt_insn_data *insnd;

  if (gimple_code(g) == GIMPLE_ASSIGN &&
      gimple_assign_rhs_class(g) == GIMPLE_SINGLE_RHS)
    {
      if (gimple_assign_rhs_code (g) == MEM_REF)
	{
	  handle_uninit(fun, bad_fun_decl, g, &gsi);
	}
      else
	{
	  handle_write(g);
	}
    }
  else if (gimple_code(g) == GIMPLE_CALL &&
	   rvtt_p (&insnd, &stmt, g))
    {
      for (unsigned int i = 0; i < gimple_call_num_args(g); i++)
	{
	  tree arg = gimple_call_arg(g, i);
	  if (VECTOR_FLOAT_TYPE_P(TREE_TYPE(arg)))
	    {
	      gimple * def_stmt = SSA_NAME_DEF_STMT (arg);

	      if (def_stmt == nullptr || gimple_code(def_stmt) == GIMPLE_NOP)
		{
		  location_t assign_location = gimple_location (def_stmt);
		  warning_at (assign_location, OPT_Wuninitialized,
			      "%qD is used uninitialized in this function", SSA_NAME_VAR (arg));

		  warn_replace_stmt(&gsi, gimple_call_lhs(g), assign_location);
		}
	    }
	}
    }
}

// This flags 3 issues that, if not "corrected" here, result in the RTL layer
// loading and/or storing a register and the resulting cryptic message about
// spilling not being implemented for SFPU.  The 3 issues are:
//  - using an uninitialized sfpu variable (looks like a read from memory)
//  - using an sfpu variable as a function parameter (also looks like a read from memory)
//  - returning sfpu variable
//
// The first two are disambiguated by looking for a function which may take an
// sfpu wrapper variable as an argument, then confirming that when the
// uninitialized variable gets hit.  I hope this will prevent a false firing
// if someone happens to use the same type name in another namespace (I don't
// see how to find the namespace from the type name...)

static void
process (function *fun, bool sfpu_warn, bool multdiv_err)
{
  basic_block bb;

  DUMP("Warning pass on: %s\n", function_name(fun));

  bool bad_fun_decl = check_function_decl(fun);

  FOR_EACH_BB_FN (bb, fun)
    {
      gimple_stmt_iterator gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gimple *g = gsi_stmt (gsi);

	  if (multdiv_err) check_for_mult_div_ops(g);
	  if (sfpu_warn) check_sfpu(fun, gsi, g, bad_fun_decl);

	  gsi_next (&gsi);
	}
    }
}

const pass_data pass_data_rvtt_warn =
{
  GIMPLE_PASS, /* type */
  "rvtt_warn", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_warn : public gimple_opt_pass
{
public:
  pass_rvtt_warn (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_warn, ctxt)
  {}

  virtual unsigned int execute (function *);
}; // class pass_rvtt_warn

} // anon namespace

/* Entry point to rvtt_warn pass.	*/
unsigned int
pass_rvtt_warn::execute (function *fun)
{
  bool sfpu_warn = flag_rvtt_warn && TARGET_RVTT;
  if (sfpu_warn || flag_rvtt_error_multdiv)
    process (fun, sfpu_warn, flag_rvtt_error_multdiv);

  return 0;
}

gimple_opt_pass *
make_pass_rvtt_warn (gcc::context *ctxt)
{
  return new pass_rvtt_warn (ctxt);
}
