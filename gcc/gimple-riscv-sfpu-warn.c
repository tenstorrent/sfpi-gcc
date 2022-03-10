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
#include <unordered_set>
#include <iostream>
#include <tuple>
#include "config/riscv/sfpu.h"

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
	  const riscv_sfpu_insn_data *insnd;
	  gcall *use_stmt;
	  if (riscv_sfpu_p(&insnd, &use_stmt, use))
	    {
	      DUMP(" found an uninitialized vector used later\n");
	      const riscv_sfpu_insn_data* loadi_insnd = riscv_sfpu_get_insn_data(riscv_sfpu_insn_data::sfploadi);

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
		  warning_at (assign_location, OPT_Wuninitialized,
			      "%qD is used uninitialized in this function", SSA_NAME_VAR (lhs));
		}

	      gimple *loadi_use_stmt = gimple_build_call (loadi_insnd->decl, 3);
	      gimple_call_set_lhs (loadi_use_stmt, lhs);
	      gimple_call_set_arg (loadi_use_stmt, 0, build_int_cst_type(build_pointer_type (void_type_node), 0));
	      gimple_call_set_arg (loadi_use_stmt, 1, build_int_cst(integer_type_node, 0));
	      gimple_call_set_arg (loadi_use_stmt, 2, build_int_cst(integer_type_node, 0));

	      gimple_set_location (loadi_use_stmt, assign_location);
	      update_stmt(loadi_use_stmt);
	      gsi_replace(gsi, loadi_use_stmt, GSI_SAME_STMT);

	      update_ssa (TODO_update_ssa);
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

  if (gimple_code(def_stmt) == GIMPLE_PHI)
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
      const riscv_sfpu_insn_data *insnd;

      return riscv_sfpu_p (&insnd, &stmt, def_stmt);
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
process (function *fun)
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

	  gsi_next (&gsi);
	}
    }
}

const pass_data pass_data_riscv_sfpu_warn =
{
  GIMPLE_PASS, /* type */
  "riscv_sfpu_warn", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_riscv_sfpu_warn : public gimple_opt_pass
{
public:
  pass_riscv_sfpu_warn (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_riscv_sfpu_warn, ctxt)
  {}

  virtual unsigned int execute (function *);
}; // class pass_riscv_sfpu_warn

} // anon namespace

/* Entry point to riscv_sfpu_warn pass.	*/
unsigned int
pass_riscv_sfpu_warn::execute (function *fun)
{
  if (flag_sfpu)
    {
      process (fun);
    }

  return 0;
}

gimple_opt_pass *
make_pass_riscv_sfpu_warn (gcc::context *ctxt)
{
  return new pass_riscv_sfpu_warn (ctxt);
}
