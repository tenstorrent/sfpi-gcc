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
#include <tuple>
#include <iostream>
#include "config/riscv/sfpu.h"

using namespace std;

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

static bool
is_int_arg(gcall *stmt, unsigned int arg)
{
  tree decl = gimple_call_arg(stmt, arg);

  return decl != nullptr && TREE_CODE(decl) == INTEGER_CST;
}

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
		      DUMP("Found a subsequent use\n");
		      return true;
		    }
		}
	    }

	  gsi_next (&gsi);
	}
    }

  return false;
}

// Return whether the call/stmt can be combined with an iadd_i
static bool
can_combine_iadd_i_ex(const riscv_sfpu_insn_data *insnd,
		      gcall *stmt,
		      bool is_sign_bit_cc)
{
  return
    (insnd->id == riscv_sfpu_insn_data::sfpiadd_i_ex &&
     (get_int_arg(stmt, 3) & SFPCMP_EX_MOD1_CC_MASK) == SFPCMP_EX_MOD1_CC_NONE) ||

    (insnd->id == riscv_sfpu_insn_data::sfpiadd_i_ex_lv &&
     (get_int_arg(stmt, 4) & SFPCMP_EX_MOD1_CC_MASK) == SFPCMP_EX_MOD1_CC_NONE) ||

    (insnd->id == riscv_sfpu_insn_data::sfpiadd_v_ex &&
     (get_int_arg(stmt, 2) & SFPCMP_EX_MOD1_CC_MASK) == SFPCMP_EX_MOD1_CC_NONE) ||

    (insnd->id == riscv_sfpu_insn_data::sfpexexp && get_int_arg(stmt, 1) == 0 && is_sign_bit_cc) ||

    (insnd->id == riscv_sfpu_insn_data::sfpexexp_lv && get_int_arg(stmt, 2) == 0 && is_sign_bit_cc);
}

// Combine candidate_stmt (an iadd_i_ex) with stmt by updating mod1/imm of
// stmt
static void
combine_iadd_i_ex(const riscv_sfpu_insn_data *insnd, gcall *stmt, gcall *candidate_stmt)
{
  int candidate_mod1 = get_int_arg(candidate_stmt, 3);

  switch (insnd->id) {
  case riscv_sfpu_insn_data::sfpiadd_i_ex:
    {
      int old_sub = get_int_arg(stmt, 3) & SFPIADD_EX_MOD1_IS_SUB;
      gimple_call_set_arg(stmt, 3,
			  build_int_cst(integer_type_node,
					(candidate_mod1 & ~SFPIADD_EX_MOD1_IS_SUB) | old_sub));
    }
    break;
  case riscv_sfpu_insn_data::sfpiadd_i_ex_lv:
    {
      int old_sub = get_int_arg(stmt, 4) & SFPIADD_EX_MOD1_IS_SUB;
      gimple_call_set_arg(stmt, 4,
			  build_int_cst(integer_type_node,
					(candidate_mod1 & ~SFPIADD_EX_MOD1_IS_SUB) | old_sub));
    }
    break;
  case riscv_sfpu_insn_data::sfpiadd_v_ex:
    {
      int old_sub = get_int_arg(stmt, 2) & SFPIADD_EX_MOD1_IS_SUB;
      gimple_call_set_arg(stmt, 2,
			  build_int_cst(integer_type_node,
					(candidate_mod1 & ~SFPIADD_EX_MOD1_IS_SUB) | old_sub));
    }
    break;
  case riscv_sfpu_insn_data::sfpexexp:
    {
      int mod1 = ((candidate_mod1 & SFPCMP_EX_MOD1_CC_MASK) == SFPCMP_EX_MOD1_CC_LT) ?
	SFPEXEXP_MOD1_SET_CC_SGN_EXP : SFPEXEXP_MOD1_SET_CC_SGN_COMP_EXP;
      gimple_call_set_arg(stmt, 1, build_int_cst(integer_type_node, mod1));
      break;
    }
  case riscv_sfpu_insn_data::sfpexexp_lv:
    {
      int mod1 = ((candidate_mod1 & SFPCMP_EX_MOD1_CC_MASK) == SFPCMP_EX_MOD1_CC_LT) ?
	SFPEXEXP_MOD1_SET_CC_SGN_EXP : SFPEXEXP_MOD1_SET_CC_SGN_COMP_EXP;
      gimple_call_set_arg(stmt, 2, build_int_cst(integer_type_node, mod1));
      break;
    }
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

// Returns true iff a stmt between start and gsi sets the CC or is not a part
// of calculating arg.	Need to follow the chain of operations so the case,
// for example, where one of the arguments gets negated is handled. Actually,
// let's just handle that case
static bool
intervening_unrelated_or_cc_stmt(tree arg, gimple_stmt_iterator start, gimple_stmt_iterator gsi)
{
  gsi_prev (&gsi);

  while (gsi.ptr != start.ptr)
  {
    gcall *stmt;
    const riscv_sfpu_insn_data *insnd;
    if (riscv_sfpu_p (&insnd ,&stmt, gsi))
      {
	if (riscv_sfpu_sets_cc(insnd, stmt))
	  {
	    return true;
	  }

	tree lhs = gimple_call_lhs(stmt);
	if (lhs != arg)
	  {
	    return true;
	  }
	else if (insnd->id == riscv_sfpu_insn_data::sfpmov)
	  {
	    arg = gimple_call_arg(stmt, 0);
	  }
      }

    gsi_prev (&gsi);
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

static bool
get_single_use(tree var, gimple **gout)
{
  bool single = false;

  use_operand_p use_p;
  imm_use_iterator iter;

  if (var != nullptr)
    {
      FOR_EACH_IMM_USE_FAST (use_p, iter, var)
	{
	  gimple *g = USE_STMT(use_p);

	  if (g->code != GIMPLE_DEBUG)
	    {
	      if (single)
		{
		  return false;
		}

	      single = true;
	      *gout = g;
	    }
	}
    }

  return single;
}

// Combine iadd_i_ex
//  - iadd_i_ex when used to set the CC but w/ no LHS combines with other CC
//    stmts which don't set the CC, e.g., iadd_i, iadd_v, exexp
//  - setcc/lz are not optimized here.	the usage pattern of that combination
//    is unlikely to show up much in real life and some cases are presently
//    handled by a peephole optimization.  Move it here if ever worthwhile
//
// Works by:
//  - find candidate add_i which sets the CC and compares against 0
//  - only combine if there are no subsequent uses of the LHS (null LHS)
//  - find the assignment of the variable used as src in the add_i
//  - ensure there are no CC stmts in between assignment and use
//  - ensure there are no other uses between assignment and use
//  - update assignment mod1 value to set the CC/imm as relevant 
//  - move the assignment stmt to the location of the candidate stmt
//  - delete the candidate iadd_i
static bool
try_combine_iadd_i_ex(const riscv_sfpu_insn_data *candidate_insnd,
		      gcall *candidate_stmt,
		      gimple_stmt_iterator candidate_gsi)
{
  bool combined = false;

  // Check for candidate iadd_i that sets the CC and compares to 0
  if (candidate_insnd->id == riscv_sfpu_insn_data::sfpiadd_i_ex &&
      is_int_arg(candidate_stmt, 2) && (get_int_arg(candidate_stmt, 2) == 0) &&
      ((get_int_arg(candidate_stmt, 3) & SFPCMP_EX_MOD1_CC_MASK) != 0) &&
      gimple_call_lhs(candidate_stmt) == nullptr)
    {
      DUMP("Trying to combine %s\n", candidate_insnd->name);

      // Got a candidate
      int mod1 = get_int_arg(candidate_stmt, 3);
      bool is_sign_bit_cc =
	((mod1 & SFPCMP_EX_MOD1_CC_MASK) == SFPCMP_EX_MOD1_CC_LT) ||
	((mod1 & SFPCMP_EX_MOD1_CC_MASK) == SFPCMP_EX_MOD1_CC_GTE);

      // Find when this variable was assigned
      gimple *assign_g = SSA_NAME_DEF_STMT(gimple_call_arg(candidate_stmt, 1));
      gcall *assign_stmt;
      const riscv_sfpu_insn_data *assign_insnd;
      bool is_sfpu = riscv_sfpu_p(&assign_insnd, &assign_stmt, assign_g);
      gimple_stmt_iterator assign_gsi = gsi_for_stmt(assign_g);

      if (is_sfpu &&
	  gsi_bb(assign_gsi) == gsi_bb(candidate_gsi) &&
	  !intervening_cc_stmt(assign_gsi, candidate_gsi) &&
	  !intervening_use(gimple_call_lhs(assign_stmt), assign_gsi, candidate_gsi))
	{
	  // Check to see if the assignment is one of the targeted optimizations
	  if (can_combine_iadd_i_ex(assign_insnd, assign_stmt, is_sign_bit_cc))
	    {
		DUMP("	combining with %s\n", assign_insnd->name);
		// Found a replaceable iadd_i
		combine_iadd_i_ex(assign_insnd, assign_stmt, candidate_stmt);

		// Move target
		gsi_move_before(&assign_gsi, &candidate_gsi);

		// Remove candidate
		unlink_stmt_vdef(candidate_stmt);
		gsi_remove(&candidate_gsi, true);
		release_defs(candidate_stmt);

		combined = true;
	    }
	}
    }

  return combined;
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

// Mads increase register pressure so need to be careful to not turn compiling
// code into non-compiling code by agressively creating mads.  Ideally, the
// programmer who runs out of registers could re-stucture their code to
// avoid the mads and get it to compile
// To that end:
//   - mads are only created if there is exactly one use of the base multiply
//   - mads are not created across CC or BB boundaries (some of which may be
//   - managable).  future work - could handle CC boundaries w/ code motion
static bool
try_gen_mad(const riscv_sfpu_insn_data *candidate_insnd,
	    gcall *candidate_stmt,
	    gimple_stmt_iterator candidate_gsi)
{
  bool combined = false;

  if (candidate_insnd->id == riscv_sfpu_insn_data::sfpadd ||
      candidate_insnd->id == riscv_sfpu_insn_data::sfpadd_lv)
    {
      DUMP("Trying to combine %s for mad\n", candidate_insnd->name);

      gimple_stmt_iterator assign_gsi;
      gcall *assign_stmt;
      const riscv_sfpu_insn_data *assign_insnd;
      int live = candidate_insnd->live;
      int which_arg = 0;

      int candidate_mod = get_int_arg(candidate_stmt, candidate_insnd->mod_pos);

      // Both the add and the mul can't have mod set
      // Not worrying about optimizing -.5 and +.5, just avoiding +.5 and +.5
      bool found_one = match_prior_assignment(riscv_sfpu_insn_data::sfpmul,
					      &assign_insnd, &assign_stmt, &assign_gsi,
					      gimple_call_arg(candidate_stmt, which_arg + live)) &&
	  (candidate_mod == 0 || get_int_arg(assign_stmt, assign_insnd->mod_pos) == 0);

      if (!found_one)
	{
	  which_arg = 1;
	  found_one = match_prior_assignment(riscv_sfpu_insn_data::sfpmul,
					     &assign_insnd, &assign_stmt, &assign_gsi,
					     gimple_call_arg(candidate_stmt, which_arg + live)) &&
	      (candidate_mod == 0 || get_int_arg(assign_stmt, assign_insnd->mod_pos) == 0);
	}

      if (found_one && gsi_bb(assign_gsi) == gsi_bb(candidate_gsi))
	{
	  DUMP("  found a matching %s...\n", assign_insnd->name);

	  tree assign_lhs = gimple_call_lhs(assign_stmt);

	  if (!intervening_unrelated_or_cc_stmt(gimple_call_arg(candidate_stmt, live + which_arg ^ 1),
						assign_gsi, candidate_gsi) &&
	      has_single_use(assign_lhs))
	    {
	      DUMP("  combine %s arg %d w/ mul\n", candidate_insnd->name, which_arg);

	      // Create mad
	      const riscv_sfpu_insn_data *mad_insnd = riscv_sfpu_get_insn_data(live ?
									       "__builtin_riscv_sfpmad_lv" :
									       "__builtin_riscv_sfpmad");
	      gimple* mad_stmt = gimple_build_call(mad_insnd->decl, 4 + live);
	      if (live)
		{
		  gimple_call_set_arg(mad_stmt, 0, gimple_call_arg(candidate_stmt, 0));
		}
	      gimple_call_set_arg(mad_stmt, live + 0, gimple_call_arg(assign_stmt, 0));
	      gimple_call_set_arg(mad_stmt, live + 1, gimple_call_arg(assign_stmt, 1));
	      gimple_call_set_arg(mad_stmt, live + 2, gimple_call_arg(candidate_stmt, live + (which_arg ^ 1)));
	      int mod = (candidate_mod != 0) ? candidate_mod : get_int_arg(assign_stmt, assign_insnd->mod_pos);
	      gimple_call_set_arg(mad_stmt, live + 3, build_int_cst(integer_type_node, mod));

	      gimple_call_set_lhs(mad_stmt, gimple_call_lhs(candidate_stmt));

	      gimple_set_location(mad_stmt, gimple_location (candidate_stmt));
	      gimple_set_modified(mad_stmt, true);
	      gsi_insert_before(&candidate_gsi, mad_stmt, GSI_SAME_STMT);

	      // Delete add
	      // Don't unlink/delete vdef since we are re-using it w/ the mad
	      gsi_remove(&candidate_gsi, true);

	      // Delete mul
	      unlink_stmt_vdef(assign_stmt);
	      gsi_remove(&assign_gsi, true);
	      release_defs(assign_stmt);

	      combined = true;
	    }
	}
    }

  return combined;
}

// Combine mul/add w/ loadi to make muli/addi
//
// We can aggessively generate mulis and addis since there is little downside
// since these instructions do not burn a register and may end up saving one.
// (The non-immediate path hasn't been optimized as of writing this code which
// could be one downside).
//
// We never combine with a "live" loadi since that register may not have the
// same value in every vector slot.  We could be intelligent here by looking
// to see if the CC state at the time of the loadi_lv is the same as the
// current CC state, but I suspect that case is uninteresting anyway.
//
// Scoping rules let us just go to town, ie, we'll never see a loadi at a
// narrower CC state than the candidate (it would be out of scope) and if it
// is at a wider state, we're good.  BBs don't matter either.
//
// However, we do need to be careful of moves since muli/addi may generate a
// move if the source is re-used latter which would be a addi+mov as more
// expensive than a load+add.
//
// Note: this doesn't optimally handle the case where both operands to
// muli/addi are from loadi and one loadi is used later while the other is not
// (in theory the code could pick the right one).  This is uninteresting as
// operating on two immediates should be done outside of SFPU anyway...
static bool
try_gen_muli_or_addi(const riscv_sfpu_insn_data *candidate_insnd,
		     gcall *candidate_stmt,
		     gimple_stmt_iterator candidate_gsi)
{
  bool combined = false;

  if (candidate_insnd->id == riscv_sfpu_insn_data::sfpmul ||
      candidate_insnd->id == riscv_sfpu_insn_data::sfpmul_lv ||
      candidate_insnd->id == riscv_sfpu_insn_data::sfpadd ||
      candidate_insnd->id == riscv_sfpu_insn_data::sfpadd_lv)
    {
      DUMP("Trying to combine %s into %si\n", candidate_insnd->name,
	   riscv_sfpu_get_notlive_version(candidate_insnd)->name);

      int live = candidate_insnd->live;
      gimple_stmt_iterator assign_gsi;
      gcall *assign_stmt;
      const riscv_sfpu_insn_data *assign_insnd;
      int which_arg = 0;

      // Only combine live if we are writing to the same arg as the dst arg
      bool found_one = (match_prior_assignment(riscv_sfpu_insn_data::sfploadi,
					       &assign_insnd, &assign_stmt, &assign_gsi,
					       gimple_call_arg(candidate_stmt, which_arg + live)) &&
			!subsequent_use(gimple_call_arg(candidate_stmt, (which_arg ^ 1) + live), candidate_gsi) &&
			gsi_bb(assign_gsi) == gsi_bb(candidate_gsi) &&
			!intervening_cc_stmt(assign_gsi, candidate_gsi) &&
			(!live || gimple_call_arg(candidate_stmt, 0) == gimple_call_arg(candidate_stmt, (which_arg ^ 1) + live)));

      if (!found_one)
	{
	  which_arg = 1;
	  found_one = (match_prior_assignment(riscv_sfpu_insn_data::sfploadi,
					      &assign_insnd, &assign_stmt, &assign_gsi,
					      gimple_call_arg(candidate_stmt, which_arg + live)) &&
		       !subsequent_use(gimple_call_arg(candidate_stmt, (which_arg ^ 1) + live), candidate_gsi) &&
		       gsi_bb(assign_gsi) == gsi_bb(candidate_gsi) &&
		       !intervening_cc_stmt(assign_gsi, candidate_gsi) &&
		       (!live || gimple_call_arg(candidate_stmt, 0) == gimple_call_arg(candidate_stmt, (which_arg ^ 1) + live)));
	}

      if (found_one)
	{
	  DUMP("  found a matching %s...\n", assign_insnd->name);

	  // muli/addi only support fp16b
	  if (get_int_arg(assign_stmt, 1) == SFPLOADI_MOD0_FLOATB)
	    {
	      char name[32];
	      sprintf(name, "__builtin_riscv_%si",
		      riscv_sfpu_get_notlive_version(candidate_insnd)->name);

	      DUMP("  combine %s arg %d w/ loadi into %s\n", candidate_insnd->name, which_arg, name);

	      // Create <add,mul>i
	      // addi/muli are "implicitly live" (dst_as_src), no explicit live versions
	      const riscv_sfpu_insn_data *opi_insnd = riscv_sfpu_get_insn_data(name);
	      gimple* opi_stmt = gimple_build_call(opi_insnd->decl, 4);
	      gimple_call_set_arg(opi_stmt, 0, gimple_call_arg(assign_stmt, 0));
	      gimple_call_set_arg(opi_stmt, 1, gimple_call_arg(candidate_stmt, live + (which_arg ^ 1)));
	      gimple_call_set_arg(opi_stmt, 2, gimple_call_arg(assign_stmt, 2));
	      gimple_call_set_arg(opi_stmt, 3, build_int_cst(integer_type_node,
							     get_int_arg(candidate_stmt, candidate_insnd->mod_pos)));

	      gimple_call_set_lhs(opi_stmt, gimple_call_lhs(candidate_stmt));

	      gimple_set_location(opi_stmt, gimple_location (candidate_stmt));
	      gimple_set_modified(opi_stmt, true);
	      gsi_insert_before(&candidate_gsi, opi_stmt, GSI_SAME_STMT);

	      // Delete op
	      unlink_stmt_vdef(candidate_stmt);
	      gsi_remove(&candidate_gsi, true);

	      combined = true;
	    }
	}
    }

  return combined;
}

// Combine addi of +/- 0.5f w/ add/mul
//
// This is tricky.  It would be great to do this step after generating mads
// and addi/muli, however, by then an add of 0.5 may be turned into a mad and
// undoing is more work than the complicated pattern search now.
//
// So, look for:
//   an add or mul
//   with only a single use which is an add of .5
static bool
try_combine_add_half(const riscv_sfpu_insn_data *candidate_insnd,
		     gcall *candidate_stmt)
{
  const int half = 0x3f00;
  const int neg_half = 0xbf00;
  bool combined = false;

  // XXXX todo: handle _lv versions.  gets complicated
  if (candidate_insnd->id == riscv_sfpu_insn_data::sfpadd ||
      candidate_insnd->id == riscv_sfpu_insn_data::sfpmul)
    {
      DUMP("Found %s, looking to see if its use is an add of +/-0.5f...\n", candidate_insnd->name);

      tree candidate_lhs = gimple_call_lhs(candidate_stmt);
      int live = candidate_insnd->live;
      gimple *use_g;
      gcall *use_stmt;
      const riscv_sfpu_insn_data *use_insnd;

      if (get_single_use(candidate_lhs, &use_g) &&
	  riscv_sfpu_p(&use_insnd, &use_stmt, use_g) &&
	  use_insnd->id == riscv_sfpu_insn_data::sfpadd)
	{
	  DUMP("  ...has a single use %s\n", use_insnd->name);

	  int which_arg = (gimple_call_arg(use_stmt, live + 0) == candidate_lhs) ? 1 : 0;
	  gimple_stmt_iterator assign_gsi;
	  gcall *assign_stmt;
	  const riscv_sfpu_insn_data *assign_insnd;
	  if (match_prior_assignment(riscv_sfpu_insn_data::sfploadi,
				     &assign_insnd, &assign_stmt, &assign_gsi,
				     gimple_call_arg(use_stmt, which_arg + live)))
	    {
	      if (is_int_arg(assign_stmt, 2))
		{
		  int offset = get_int_arg(assign_stmt, 2);

		  if (offset == half || offset == neg_half)
		    {
		      DUMP("  ...has a loadi of 0.5\n");

		      int mod1 = (offset == half) ? SFPMAD_MOD1_OFFSET_POSH : SFPMAD_MOD1_OFFSET_NEGH;
		      gimple_call_set_arg(candidate_stmt, candidate_insnd->mod_pos,
					  build_int_cst(integer_type_node, mod1));

		      gimple_call_set_lhs(candidate_stmt, gimple_call_lhs(use_stmt));
		      gimple_set_modified(candidate_stmt, true);

		      gimple_stmt_iterator gsi = gsi_for_stmt(use_stmt);
		      gsi_remove(&gsi, true);

		      combined = true;
		    }
		}
	    }
	}
    }

  return combined;
}

static bool
remove_unused_loadis(function *fun)
{
  DUMP("Checking for unused loadi(s)\n");

  bool removed = false;
  basic_block bb;
  gimple_stmt_iterator gsi;

  FOR_EACH_BB_FN (bb, fun)
    {
      gsi = gsi_start_bb(bb);
      while (!gsi_end_p(gsi))
	{
	  gcall *stmt;
	  const riscv_sfpu_insn_data *insnd;
	  if (riscv_sfpu_p(&insnd, &stmt, gsi))
	    {
	      tree lhs = gimple_call_lhs(stmt);
	      if (insnd->id == riscv_sfpu_insn_data::sfploadi &&
		  (lhs == nullptr || has_zero_uses(lhs)))
		{
		  DUMP("  removing %s %p %p\n", insnd->name, stmt, lhs);

		  unlink_stmt_vdef(stmt);
		  gsi_remove(&gsi, true);
		  release_defs(stmt);

		  removed = true;
		}
	    }
	  gsi_next (&gsi);
	}
    }

  return removed;
}

// Optimize stmt sequence by combining builtins.  Handles:
//   - add/mul w/ one operand a loadi of .5, use mod to handle .5
//   - iadd_i_ex w/ iadd_i_ex, exexp
//   - add w/ mul to form mad
//   - mul or add w/ loadi to make muli/addi
// The order of the half/mad/addi+muli matter, the progression of this
// ordering eliminates the need, for example, of turning a mad w/ the add of
// .5 into a mul then trying to recombine into a mad w/ a subsequent add
static void transform (function *fun)
{
  basic_block bb;
  gimple_stmt_iterator candidate_gsi;

  // Pass one: combine iadd
  FOR_EACH_BB_FN (bb, fun)
    {
      bool update = false;

      candidate_gsi = gsi_start_bb (bb);
      while (!gsi_end_p (candidate_gsi))
	{
	  gcall *candidate_stmt;
	  const riscv_sfpu_insn_data *candidate_insnd;

	  if (riscv_sfpu_p(&candidate_insnd, &candidate_stmt, candidate_gsi))
	    {
	      update |= try_combine_add_half(candidate_insnd, candidate_stmt);
	    }

	  gsi_next (&candidate_gsi);
	}

      update |= remove_unused_loadis(fun);
      if (update) update_ssa(TODO_update_ssa);

      update = false;
      candidate_gsi = gsi_start_bb (bb);
      while (!gsi_end_p (candidate_gsi))
	{
	  gcall *candidate_stmt;
	  const riscv_sfpu_insn_data *candidate_insnd;

	  if (riscv_sfpu_p(&candidate_insnd, &candidate_stmt, candidate_gsi))
	    {
	      update |= try_combine_iadd_i_ex(candidate_insnd, candidate_stmt, candidate_gsi);
	      update |= try_gen_mad(candidate_insnd, candidate_stmt, candidate_gsi);
	    }

	  gsi_next (&candidate_gsi);
	}

      if (update) update_ssa(TODO_update_ssa);

      update = false;
      candidate_gsi = gsi_start_bb (bb);
      while (!gsi_end_p (candidate_gsi))
	{
	  gcall *candidate_stmt;
	  const riscv_sfpu_insn_data *candidate_insnd;

	  if (riscv_sfpu_p(&candidate_insnd, &candidate_stmt, candidate_gsi))
	    {
		update |= try_gen_muli_or_addi(candidate_insnd, candidate_stmt, candidate_gsi);
	    }

	  gsi_next (&candidate_gsi);
	}

      update |= remove_unused_loadis(fun);
      if (update) update_ssa(TODO_update_ssa);
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
  if (flag_sfpu)
    {
      transform (fun);
    }

  return 0;
}

gimple_opt_pass *
make_pass_riscv_sfpu_combine (gcc::context *ctxt)
{
  return new pass_riscv_sfpu_combine (ctxt);
}
