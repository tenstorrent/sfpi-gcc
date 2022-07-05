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
#include <vector>
#include "config/riscv/sfpu.h"

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

using namespace std;

static std::vector<tree> load_imm_map;

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
finish_new_insn(gimple_stmt_iterator *gsip, bool insert_before, gimple *new_stmt, gimple *stmt)
{
  gcc_assert(new_stmt != nullptr);
  gimple_set_location (new_stmt, gimple_location (stmt));
  gimple_set_block (new_stmt, gimple_block (stmt));
  update_stmt (new_stmt);
  if (insert_before)
    {
      gsi_insert_before(gsip, new_stmt, GSI_SAME_STMT);
    }
  else
    {
      gsi_insert_after(gsip, new_stmt, GSI_SAME_STMT);
    }
}

static tree
emit_sfpxloadi(int mod,
	       const riscv_sfpu_insn_data *insnd,
	       gcall *stmt, gimple_stmt_iterator *gsip)
{
  const riscv_sfpu_insn_data *new_insnd =
    riscv_sfpu_get_insn_data(riscv_sfpu_insn_data::sfpxloadi);

  tree addr = gimple_call_arg(stmt, 0);
  tree val = gimple_call_arg(stmt, insnd->nonimm_pos);
  unsigned int unique_id = get_int_arg(stmt, insnd->nonimm_pos + 2);

  gcall *new_stmt = gimple_build_call(new_insnd->decl, 5);
  gimple_call_set_arg(new_stmt, 0, addr);
  gimple_call_set_arg(new_stmt, 1, build_int_cst(integer_type_node, mod));
  gimple_call_set_arg(new_stmt, 2, val);
  gimple_call_set_arg(new_stmt, 3, build_int_cst(integer_type_node, 0));
  gimple_call_set_arg(new_stmt, 4, build_int_cst(integer_type_node, 0));
  tree lhs = make_ssa_name (build_vector_type(float_type_node, 64), new_stmt);
  gimple_call_set_lhs(new_stmt, lhs);
  finish_new_insn(gsip, true, new_stmt, stmt);

  riscv_sfpu_link_nonimm_prologue(load_imm_map, unique_id,
				  gimple_call_arg(stmt, insnd->nonimm_pos + 1),
				  new_insnd, new_stmt);

  return lhs;
}

static void
emit_sfpxloadi_lv(tree lhs, tree lower, unsigned int unique_id,
		  const riscv_sfpu_insn_data *insnd,
		  gcall *stmt,
		  gimple_stmt_iterator *gsip,
		  bool before)
{
  // Note: the 2nd part of the 2 step loadi is "_lv" to preserve the
  // register assignment of the first step.  This is "elegantly" (hackily?)
  // leveraged to handle the shifting/masking by ensuring the 2nd load
  // is the UPPER bits

  const riscv_sfpu_insn_data *new_insnd =
    riscv_sfpu_get_insn_data(riscv_sfpu_insn_data::sfpxloadi_lv);

  gcall *new_stmt = gimple_build_call(new_insnd->decl, 6);
  gimple_call_set_arg(new_stmt, 0, gimple_call_arg(stmt, 0));  // pointer
  gimple_call_set_arg(new_stmt, 1, lower);                     // live
  gimple_call_set_arg(new_stmt, 2, build_int_cst(integer_type_node, SFPLOADI_MOD0_UPPER)); // mod
  gimple_call_set_arg(new_stmt, 3, gimple_call_arg(stmt, insnd->nonimm_pos));  // nonimm orig
  gimple_call_set_arg(new_stmt, 4, build_int_cst(integer_type_node, 0)); // sum
  gimple_call_set_arg(new_stmt, 5, build_int_cst(integer_type_node, 0)); // id
  gimple_call_set_lhs(new_stmt, lhs);

  riscv_sfpu_link_nonimm_prologue(load_imm_map, unique_id,
				  gimple_call_arg(stmt, insnd->nonimm_pos + 1),
				  new_insnd, new_stmt);
  finish_new_insn(gsip, before, new_stmt, stmt);
}

static tree
emit_32bit_sfpxloads(const riscv_sfpu_insn_data *insnd,
		     gcall *stmt,
		     gimple_stmt_iterator *gsip)
{
  gcc_assert(!flag_grayskull);
  tree tmp1 = emit_sfpxloadi(SFPLOADI_MOD0_LOWER, insnd, stmt, gsip);
  tree tmp2 = make_ssa_name (build_vector_type(float_type_node, 64), stmt);
  emit_sfpxloadi_lv(tmp2, tmp1, get_int_arg(stmt, insnd->nonimm_pos + 2) + 1, insnd, stmt, gsip, true);
  return tmp2;
}

static void
emit_sfpsetman_v(tree val, gimple *stmt, gimple_stmt_iterator *gsip)
{
  const riscv_sfpu_insn_data *new_insnd =
    riscv_sfpu_get_insn_data(riscv_sfpu_insn_data::sfpsetman_v);

  gimple *new_stmt = gimple_build_call(new_insnd->decl, 2);
  gimple_call_set_arg(new_stmt, 0, val);
  gimple_call_set_arg(new_stmt, 1, gimple_call_arg(stmt, 4));
  gimple_call_set_lhs(new_stmt, gimple_call_lhs(stmt));
  finish_new_insn(gsip, true, new_stmt, stmt);
}

static void
emit_sfpxfcmpv(tree val,
	       const riscv_sfpu_insn_data *insnd,
	       gcall *stmt,
	       gimple_stmt_iterator *gsip)
{
  const riscv_sfpu_insn_data *new_insnd =
    riscv_sfpu_get_insn_data(riscv_sfpu_insn_data::sfpxfcmpv);

  gimple *new_stmt = gimple_build_call(new_insnd->decl, 3);
  int mod = get_int_arg(stmt, insnd->mod_pos) & SFPXCMP_MOD1_CC_MASK;
  gimple_call_set_arg(new_stmt, 0, gimple_call_arg(stmt, 1));
  gimple_call_set_arg(new_stmt, 1, val);
  gimple_call_set_arg(new_stmt, 2, build_int_cst(integer_type_node, mod));
  gimple_call_set_lhs(new_stmt, gimple_call_lhs(stmt));
  finish_new_insn(gsip, true, new_stmt, stmt);
}

static void
emit_sfpxicmpv(tree val,
	       const riscv_sfpu_insn_data *insnd,
	       gcall *stmt,
	       gimple_stmt_iterator *gsip)
{
  const riscv_sfpu_insn_data *new_insnd =
    riscv_sfpu_get_insn_data(riscv_sfpu_insn_data::sfpxicmpv);

  gimple *new_stmt = gimple_build_call(new_insnd->decl, 3);
  int mod = get_int_arg(stmt, insnd->mod_pos) & SFPXCMP_MOD1_CC_MASK;
  gimple_call_set_arg(new_stmt, 0, val);
  gimple_call_set_arg(new_stmt, 1, gimple_call_arg(stmt, 1));
  gimple_call_set_arg(new_stmt, 2, build_int_cst(integer_type_node, mod));
  gimple_call_set_lhs(new_stmt, gimple_call_lhs(stmt));
  finish_new_insn(gsip, true, new_stmt, stmt);
}

static void
emit_sfpxiadd_v(tree val,
		int mod,
		gcall *stmt,
		gimple_stmt_iterator *gsip)
{
  const riscv_sfpu_insn_data *new_insnd =
    riscv_sfpu_get_insn_data(riscv_sfpu_insn_data::sfpxiadd_v);

  gimple *new_stmt = gimple_build_call(new_insnd->decl, 3);
  gimple_call_set_arg(new_stmt, 0, val);
  gimple_call_set_arg(new_stmt, 1, gimple_call_arg(stmt, 1));
  gimple_call_set_arg(new_stmt, 2, build_int_cst(integer_type_node, mod));
  gimple_call_set_lhs(new_stmt, gimple_call_lhs(stmt));
  finish_new_insn(gsip, true, new_stmt, stmt);
}

static void
expand_complex(gcall *stmt, const riscv_sfpu_insn_data *insnd, gimple_stmt_iterator *gsip)
{
  switch (insnd->id) {
  case riscv_sfpu_insn_data::sfpxloadi:
    if ((get_int_arg(stmt, insnd->mod_pos) & SFPXLOADI_MOD0_32BIT_MASK) != 0)
      {
	gcc_assert(!flag_grayskull);
	DUMP("  expanding 32 bit %s, replace mod and emit %s\n", insnd->name, (insnd + 1)->name);

	gimple_call_set_arg(stmt, insnd->mod_pos, build_int_cst(integer_type_node, SFPLOADI_MOD0_LOWER));
	tree lhs = gimple_call_lhs(stmt);
	tree tmp = make_ssa_name (build_vector_type(float_type_node, 64), stmt);
	gimple_call_set_lhs(stmt, tmp);
	emit_sfpxloadi_lv(lhs, tmp, get_int_arg(stmt, insnd->nonimm_pos + 2) + 1, insnd, stmt, gsip, false);
      }
    break;

  case riscv_sfpu_insn_data::sfpsetman_i:
    gcc_assert(get_int_arg(stmt, insnd->mod_pos) != SFPXSETMAN_MOD1_16BITIMM);
      {
	DUMP("  expanding %s to sfpxloadi+sfpsetman_v\n", insnd->name);
	tree tmp = flag_grayskull ?
	  emit_sfpxloadi(SFPLOADI_MOD0_USHORT, insnd, stmt, gsip) :
	  emit_32bit_sfpxloads(insnd, stmt, gsip);
	emit_sfpsetman_v(tmp, stmt, gsip);
	gsi_remove(gsip, true);
	gsi_prev(gsip);
      }
    break;

  case riscv_sfpu_insn_data::sfpxicmps:
      {
	DUMP("  expanding %s to sfpxloadi+sfpxicmpv\n", insnd->name);
	tree tmp = flag_grayskull ?
	  emit_sfpxloadi(SFPLOADI_MOD0_USHORT, insnd, stmt, gsip) :
	  emit_32bit_sfpxloads(insnd, stmt, gsip);
	emit_sfpxicmpv(tmp, insnd, stmt, gsip);
	gsi_remove(gsip, true);
	gsi_prev(gsip);
      }
    break;

  case riscv_sfpu_insn_data::sfpxfcmps:
      {
	DUMP("  expanding %s to sfpxloadi+sfpxfcmpv\n", insnd->name);
	int fmt = get_int_arg(stmt, insnd->mod_pos) & SFPXSCMP_MOD1_FMT_MASK;
	tree tmp = (fmt == SFPXSCMP_MOD1_FMT_A || fmt == SFPXSCMP_MOD1_FMT_B) ?
	  emit_sfpxloadi((fmt == SFPXSCMP_MOD1_FMT_A) ? SFPLOADI_MOD0_FLOATA : SFPLOADI_MOD0_FLOATB,
			 insnd, stmt, gsip) :
	  emit_32bit_sfpxloads(insnd, stmt, gsip);

	emit_sfpxfcmpv(tmp, insnd, stmt, gsip);
	gsi_remove(gsip, true);
	gsi_prev(gsip);
      }
    break;

  case riscv_sfpu_insn_data::sfpxiadd_i:
      {
	DUMP("  expanding %s to sfpxloadi+sfpxiadd_v\n", insnd->name);
	int mod = get_int_arg(stmt, insnd->mod_pos);
	// Only supports 32 bit non-imm loads for wh so far...
	tree tmp = (flag_grayskull) ?
	  emit_sfpxloadi((mod & SFPXIADD_MOD1_SIGNED) ? SFPLOADI_MOD0_SHORT : SFPLOADI_MOD0_USHORT,
			 insnd, stmt, gsip) :
	  emit_32bit_sfpxloads(insnd, stmt, gsip);

	emit_sfpxiadd_v(tmp, mod & SFPXIADD_MOD1_IS_SUB, stmt, gsip);
	gsi_remove(gsip, true);
	gsi_prev(gsip);
	gsi_prev(gsip);
      }
    break;

  default:
    // Do nothing
    break;
  }

  update_ssa (TODO_update_ssa);
}

// This pass does 2 things:
//   - it "expands" (lowers) some non-imm insns from the wrapper to multiple
//     insns.  e.g., an integer add of a non-imm value must be converted to
//     an integer load followed by an add.  This must happen here so that the
//     correct shifting/masking of the nonimm can be done (this is done for
//     immediate values in the expand pass).
//   - any prologue incorrectly generated for immediate insns is deleted (see
//     nonimm tag pass)
static void
transform (function *fun)
{
  DUMP("Expand-nonimm pass on: %s\n", function_name(fun));

  load_imm_map.reserve(20);

  basic_block bb;
  gimple_stmt_iterator gsi;

  bool updated = false;
  FOR_EACH_BB_FN (bb, fun)
    {
      gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gcall *stmt;
	  const riscv_sfpu_insn_data *insnd;

	  if (riscv_sfpu_p(&insnd, &stmt, gsi) &&
	      insnd->nonimm_pos != -1)
	    {
	      tree immarg = gimple_call_arg(stmt, insnd->nonimm_pos);
	      if (TREE_CODE(immarg) == SSA_NAME)
		{
		  expand_complex(stmt, insnd, &gsi);
		}
	      else
		{
		  // We have an immediate value, clean up the prologue
		  // incorrectly generated in the tag pass
		  gimple_call_set_arg(stmt, insnd->nonimm_pos + 1, build_int_cst(integer_type_node, 0));
		  update_stmt(stmt);
		}
	      updated = true;
	    }

	  gsi_next(&gsi);
	}
    }

  if (updated)
    {
      update_ssa (TODO_update_ssa);

      // Cleanup: remove unused insns created by the nonimm mess...
      // Find load_immediates which will be in 1 of 3 states:
      //  - no uses, delete
      //  - one use in an assignment which is a sum, which is unused, delete both
      //  - one use in an assignment which is a sum which is used
      DUMP("  expand-nonimm cleanup...\n");
      riscv_sfpu_cleanup_nonimm_lis(fun);
      load_imm_map.resize(0);
      update_ssa (TODO_update_ssa);
    }
}

namespace {

const pass_data pass_data_riscv_sfpu_nonimm_expand =
{
  GIMPLE_PASS, /* type */
  "riscv_sfpu_nonimm_expand", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_riscv_sfpu_nonimm_expand : public gimple_opt_pass
{
public:
  pass_riscv_sfpu_nonimm_expand (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_riscv_sfpu_nonimm_expand, ctxt)
  {}

  virtual unsigned int execute (function *);
}; // class pass_riscv_sfpu_nonimm_expand

} // anon namespace

/* Entry point to riscv_sfpu_nonimm_expand pass.	*/
unsigned int
pass_riscv_sfpu_nonimm_expand::execute (function *fun)
{
  if (flag_grayskull || flag_wormhole)
    {
      transform (fun);
    }
  return 0;
}

gimple_opt_pass *
make_pass_riscv_sfpu_nonimm_expand (gcc::context *ctxt)
{
  return new pass_riscv_sfpu_nonimm_expand (ctxt);
}
