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

static void process_tree(gimple_stmt_iterator *pre_gsip, gimple_stmt_iterator *post_gsip,
			 bool *negated, gcall *stmt, bool negate);

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

static void
remove_stmt(gcall *stmt)
{
  riscv_sfpu_prep_stmt_for_deletion(stmt);
  unlink_stmt_vdef(stmt);
  gimple_stmt_iterator gsi = gsi_for_stmt(stmt);
  gsi_remove(&gsi, true);
  release_defs(stmt);
}

static int
negate_cmp_mod(int mod)
{
    int op = mod & SFPXCMP_MOD1_CC_MASK;
    int new_op;

    switch (op) {
    case SFPXCMP_MOD1_CC_LT:
	new_op = SFPXCMP_MOD1_CC_GTE;
	break;
    case SFPXCMP_MOD1_CC_NE:
	new_op = SFPXCMP_MOD1_CC_EQ;
	break;
    case SFPXCMP_MOD1_CC_GTE:
	new_op = SFPXCMP_MOD1_CC_LT;
	break;
    case SFPXCMP_MOD1_CC_EQ:
	new_op = SFPXCMP_MOD1_CC_NE;
	break;
    case SFPXCMP_MOD1_CC_LTE:
	new_op = SFPXCMP_MOD1_CC_GT;
	break;
    case SFPXCMP_MOD1_CC_GT:
	new_op = SFPXCMP_MOD1_CC_LTE;
	break;
    }

    return (mod & ~SFPXCMP_MOD1_CC_MASK) | new_op;
}

static bool
cmp_issues_compc(int mod)
{
  return (mod & SFPXCMP_MOD1_CC_MASK) == SFPXCMP_MOD1_CC_LTE;
}

static int get_bool_type(int op, bool negate)
{
  if (op == SFPXBOOL_MOD1_OR) return negate ? SFPXBOOL_MOD1_AND : SFPXBOOL_MOD1_OR;
  if (op == SFPXBOOL_MOD1_AND) return negate ? SFPXBOOL_MOD1_OR : SFPXBOOL_MOD1_AND;
  gcc_assert(0);
}

static int
flip_negated_cmp(gcall *stmt, const riscv_sfpu_insn_data *insnd, bool negate)
{
  int mod = get_int_arg(stmt, insnd->mod_pos);
  if (negate)
    {
      // Flip the operation
      mod = negate_cmp_mod(mod);
      gimple_call_set_arg(stmt, insnd->mod_pos, build_int_cst(integer_type_node, mod));
    }

  return mod;
}

static gcall*
copy_and_replace_icmp(gcall *stmt, riscv_sfpu_insn_data::insn_id id)
{
  const riscv_sfpu_insn_data *new_insnd = riscv_sfpu_get_insn_data(id);
  int nargs = gimple_call_num_args(stmt);
  gcall * new_stmt = gimple_build_call(new_insnd->decl, nargs);
  for (int i = 0; i < nargs; i++)
    {
      gimple_call_set_arg(new_stmt, i, gimple_call_arg(stmt, i));
    }
  gimple_set_location(new_stmt, gimple_location(stmt));
  // The icmp returns an int used in the boolean tree, the iadd return nothing
  gimple_call_set_lhs(new_stmt, NULL_TREE);
  gimple_set_vuse(new_stmt, gimple_vuse(stmt));
  gimple_stmt_iterator gsi = gsi_for_stmt(stmt);
  gsi_insert_before(&gsi, new_stmt, GSI_SAME_STMT);
  remove_stmt(stmt);

  // Make the iadd do a subtract for the compare
  // Make sure other code knows this is a compare
  int mod = get_int_arg(new_stmt, new_insnd->mod_pos) | SFPXIADD_MOD1_IS_SUB;
  if (id == riscv_sfpu_insn_data::sfpxiadd_i)
    {
      mod |= SFPXIADD_MOD1_DST_UNUSED;
    }
  gimple_call_set_arg(new_stmt, new_insnd->mod_pos, build_int_cst(integer_type_node, mod));

  return new_stmt;
}

static void
finish_new_insn(gimple_stmt_iterator *gsip, bool insert_before, gimple *new_stmt, gcall *stmt)
{
  gcc_assert(new_stmt != nullptr);
  gimple_set_location (new_stmt, gimple_location (stmt));
  gimple_set_block (new_stmt, gimple_block (stmt));
  update_stmt (new_stmt);
  if (insert_before)
    {
      gsi_insert_before(gsip, new_stmt, GSI_NEW_STMT);
    }
  else
    {
      gsi_insert_after(gsip, new_stmt, GSI_NEW_STMT);
    }
}

static void
emit_pushc(gimple_stmt_iterator *gsip, gcall *stmt)
{
  const riscv_sfpu_insn_data *new_insnd =
    riscv_sfpu_get_insn_data(riscv_sfpu_insn_data::sfppushc);
  gimple *new_stmt;
  if (flag_grayskull)
    {
      new_stmt = gimple_build_call(new_insnd->decl, 0);
    }
  else
    {
      new_stmt = gimple_build_call(new_insnd->decl, 1, size_int(SFPPUSHCC_MOD1_PUSH));
    }
  finish_new_insn(gsip, true, new_stmt, stmt);
}

static void
emit_popc(gimple_stmt_iterator *gsip, gcall *stmt)
{
  const riscv_sfpu_insn_data *new_insnd =
    riscv_sfpu_get_insn_data(riscv_sfpu_insn_data::sfppopc);
  gimple *new_stmt;
  if (flag_grayskull)
    {
      new_stmt = gimple_build_call(new_insnd->decl, 0);
    }
  else
    {
      new_stmt = gimple_build_call(new_insnd->decl, 1, size_int(SFPPOPCC_MOD1_POP));
    }
  finish_new_insn(gsip, false, new_stmt, stmt);
}

static void
emit_compc(gimple_stmt_iterator *gsip, gcall *stmt)
{
  const riscv_sfpu_insn_data *new_insnd =
    riscv_sfpu_get_insn_data(riscv_sfpu_insn_data::sfpcompc);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 0);
  finish_new_insn(gsip, false, new_stmt, stmt);
}

static tree
emit_loadi(gimple_stmt_iterator *gsip, gcall *stmt, int val)
{
  const riscv_sfpu_insn_data *new_insnd =
    riscv_sfpu_get_insn_data(riscv_sfpu_insn_data::sfpxloadi);
  tree nullp = build_int_cst (build_pointer_type (void_type_node), 0);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 3, nullp, size_int(SFPLOADI_MOD0_SHORT), size_int(val));

  tree tmp = make_ssa_name (build_vector_type(float_type_node, 64), new_stmt);
  gimple_call_set_lhs (new_stmt, tmp);

  finish_new_insn(gsip, true, new_stmt, stmt);

  return tmp;
}

static tree
emit_loadi_lv(gimple_stmt_iterator *gsip, gcall *stmt, tree in, int val)
{
  const riscv_sfpu_insn_data *new_insnd =
    riscv_sfpu_get_insn_data(riscv_sfpu_insn_data::sfpxloadi_lv);
  tree nullp = build_int_cst (build_pointer_type (void_type_node), 0);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 4, nullp, in, size_int(SFPLOADI_MOD0_SHORT), size_int(val));
  gimple_set_vuse(new_stmt, in); // XXXXX

  tree tmp = make_ssa_name (build_vector_type(float_type_node, 64), new_stmt);
  gimple_call_set_lhs (new_stmt, tmp);

  finish_new_insn(gsip, false, new_stmt, stmt);

  return tmp;
}

static void
emit_setcc_v(gimple_stmt_iterator *gsip, gcall *stmt, tree in)
{
  const riscv_sfpu_insn_data *new_insnd =
    riscv_sfpu_get_insn_data(riscv_sfpu_insn_data::sfpsetcc_v);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 2, in, size_int(SFPSETCC_MOD1_LREG_EQ0));
  gimple_set_vuse(new_stmt, in); // XXXXX
  finish_new_insn(gsip, false, new_stmt, stmt);
}

// Handle AND and OR conditionals
//
// Recursively processes a tree of boolean expressions.	 ORs are converted to
// ANDs by negating the children of the current node.  The negation is toggled
// as the tree is traversed to avoid accumulating redundant negations.
//
// Descending the LHS uses the last PUSHC as the "fence" against which a COMPC
// can be issued, however, descending the RHS would mess up the results from
// the LHS w/o a new fence, hence the PUSHC prior to the RHS.  The POPC would
// destroy the results of the RHS and so those results are saved/restored with
// saved_enables.
static void
process_bool_tree(gimple_stmt_iterator *pre_gsip, gimple_stmt_iterator *post_gsip,
		  bool *negated, gcall *stmt, int op, bool negate)
{
  DUMP("    process %s n:%d\n", op == SFPXBOOL_MOD1_AND ? "AND" : "OR", negate);

  bool negate_node = false;
  if (get_bool_type(op, negate) == SFPXBOOL_MOD1_OR)
    {
      negate_node = true;
      negate = !negate;
    }

  // Emit LEFT
  gimple_stmt_iterator left_post_gsi;
  bool left_negated = false;
  DUMP("    left\n");
  process_tree(pre_gsip, &left_post_gsi, &left_negated,
	       dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, 1))), negate);

  // Emit RIGHT
  gimple_stmt_iterator right_pre_gsi;
  bool right_negated = false;
  DUMP("    right\n");
  process_tree(&right_pre_gsi, post_gsip, &right_negated,
	       dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, 2))), negate);

  if (right_negated)
    {
      DUMP("	right negated, emitting pre/post\n");
      emit_pushc(&right_pre_gsi, stmt);
      tree saved_enables = emit_loadi(&right_pre_gsi, stmt, 1);
      saved_enables = emit_loadi_lv(post_gsip, stmt, saved_enables, 0);
      emit_popc(post_gsip, stmt);
      emit_setcc_v(post_gsip, stmt, saved_enables);
    }

  if (negate_node)
    {
      DUMP("	node negated, emitting compc\n");
      *negated = true;
      emit_compc(post_gsip, stmt);
    }

  if (left_negated)
    {
      // Parent needs a fence for this node's left and side (if the parent
      // isn't the root)
      *negated = true;
    }

  DUMP("    exiting bool %d %d\n", op, negate);
}

static void
process_tree(gimple_stmt_iterator *pre_gsip, gimple_stmt_iterator *post_gsip,
	     bool *negated, gcall *stmt, bool negate)
{
  const riscv_sfpu_insn_data *insnd = riscv_sfpu_get_insn_data(stmt);
  DUMP("  process %s n:%d\n", insnd->name, negate);

  switch (insnd->id)
    {
    case riscv_sfpu_insn_data::sfpxfcmps:
    case riscv_sfpu_insn_data::sfpxfcmpv:
      {
	int mod = flip_negated_cmp(stmt, insnd, negate);
	if (cmp_issues_compc(mod)) *negated = true;
	gimple_call_set_lhs (stmt, NULL_TREE);
	*pre_gsip = *post_gsip = gsi_for_stmt(stmt);
      }
      break;

    case riscv_sfpu_insn_data::sfpxicmps:
    case riscv_sfpu_insn_data::sfpxicmpv:
      {
	// iadd insns return a vector while icmp insns return an int, remap
	int mod = flip_negated_cmp(stmt, insnd, negate);
	if (cmp_issues_compc(mod)) *negated = true;
	stmt = copy_and_replace_icmp(stmt, (insnd->id == riscv_sfpu_insn_data::sfpxicmps) ?
				     riscv_sfpu_insn_data::sfpxiadd_i : riscv_sfpu_insn_data::sfpxiadd_v);
	*pre_gsip = *post_gsip = gsi_for_stmt(stmt);
      }
      break;

    case riscv_sfpu_insn_data::sfpxbool:
      {
	  int op = get_int_arg(stmt, 0);
	  if (op == SFPXBOOL_MOD1_NOT)
	    {
	      process_tree(pre_gsip, post_gsip, negated,
			   dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, 1))), !negate);
	    }
	  else
	    {
	      process_bool_tree(pre_gsip, post_gsip, negated, stmt, op, negate);
	    }
	  remove_stmt(stmt);
      }
      break;

    default:
      fprintf(stderr, "Illegal riscv sfpu builtin found in conditional tree: %s\n", insnd->name);
      gcc_assert(0);
    }
}

// Expand boolean trees
//
// The hardware does not support OR and generates some comparisons (LTE, GE)
// by ANDing others together and issuing a compc.  This requires refactoring
// boolean expressions using De Moragan's laws.	 The root of a tree is anchored
// by an sfpxcond.  All dependent operations are chained to this by their
// return values.  This pass traverses the tree, more or less deletes it and
// replaces it with one that works w/ the HW.
static void
transform (function *fun)
{
  DUMP("Expand pass on: %s\n", function_name(fun));

  basic_block bb;
  gimple_stmt_iterator gsi;
  FOR_EACH_BB_FN (bb, fun)
    {
      gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gcall *stmt;
	  const riscv_sfpu_insn_data *insnd;

	  if (riscv_sfpu_p(&insnd, &stmt, gsi) &&
	      insnd->id == riscv_sfpu_insn_data::sfpxcond)
	    {
	      gimple_stmt_iterator next_gsi = gsi;
	      gsi_next(&next_gsi);
	      bool negated = false;
	      gimple_stmt_iterator pre_gsi, post_gsi;
	      process_tree(&pre_gsi, &post_gsi, &negated,
			   dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, 0))), false);
	      remove_stmt(stmt);
	      gsi = next_gsi;
	      update_ssa (TODO_update_ssa);
	    }
	  else
	    {
	      gsi_next (&gsi);
	    }
	}
    }
}

namespace {

const pass_data pass_data_riscv_sfpu_expand =
{
  GIMPLE_PASS, /* type */
  "riscv_sfpu_expand", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_riscv_sfpu_expand : public gimple_opt_pass
{
public:
  pass_riscv_sfpu_expand (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_riscv_sfpu_expand, ctxt)
  {}

  virtual unsigned int execute (function *);
}; // class pass_riscv_sfpu_expand

} // anon namespace

/* Entry point to riscv_sfpu_expand pass.	*/
unsigned int
pass_riscv_sfpu_expand::execute (function *fun)
{
  if (flag_grayskull || flag_wormhole)
    {
      transform (fun);
    }

  return 0;
}

gimple_opt_pass *
make_pass_riscv_sfpu_expand (gcc::context *ctxt)
{
  return new pass_riscv_sfpu_expand (ctxt);
}
