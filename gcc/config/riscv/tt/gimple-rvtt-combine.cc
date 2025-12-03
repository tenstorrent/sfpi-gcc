/* Pass to combine SFPU insns
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
#include <vector>
#include "rvtt.h"

using namespace std;

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

static std::vector<tree> load_imm_map;

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
		      DUMP("  found a subsequent use\n");
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
can_combine_sfpxiadd_i(const rvtt_insn_data *insnd,
		       gcall *stmt,
		       bool is_sign_bit_cc)
{
  return
    (insnd->id == rvtt_insn_data::sfpxiadd_i &&
     (get_int_arg(stmt, insnd->mod_pos) & SFPXCMP_MOD1_CC_MASK) == SFPXCMP_MOD1_CC_NONE) ||

    (insnd->id == rvtt_insn_data::sfpxiadd_i_lv &&
     (get_int_arg(stmt, insnd->mod_pos) & SFPXCMP_MOD1_CC_MASK) == SFPXCMP_MOD1_CC_NONE) ||

    (insnd->id == rvtt_insn_data::sfpxiadd_v &&
     (get_int_arg(stmt, insnd->mod_pos) & SFPXCMP_MOD1_CC_MASK) == SFPXCMP_MOD1_CC_NONE) ||

    (insnd->id == rvtt_insn_data::sfpexexp && get_int_arg(stmt, insnd->mod_pos) == 0 && is_sign_bit_cc) ||

    (insnd->id == rvtt_insn_data::sfpexexp_lv && get_int_arg(stmt, insnd->mod_pos) == 0 && is_sign_bit_cc);
}

// Combine candidate_stmt (an sfpxiadd_i) with stmt by updating mod1/imm of
// stmt
static void
combine_sfpxiadd_i(const rvtt_insn_data *insnd, gcall *stmt,
		   const rvtt_insn_data *candidate_insnd, gcall *candidate_stmt)
{
  int candidate_mod1 = get_int_arg(candidate_stmt, candidate_insnd->mod_pos);

  switch (insnd->id) {
  case rvtt_insn_data::sfpxiadd_i:
    {
      int old_sub = get_int_arg(stmt, insnd->mod_pos) & SFPXIADD_MOD1_IS_SUB;
      gimple_call_set_arg(stmt, insnd->mod_pos,
			  build_int_cst(integer_type_node,
					(candidate_mod1 & ~(SFPXIADD_MOD1_IS_SUB | SFPXIADD_MOD1_DST_UNUSED)) |
					old_sub));
    }
    break;
  case rvtt_insn_data::sfpxiadd_i_lv:
    {
      int old_sub = get_int_arg(stmt, insnd->mod_pos) & SFPXIADD_MOD1_IS_SUB;
      gimple_call_set_arg(stmt, insnd->mod_pos,
			  build_int_cst(integer_type_node,
					(candidate_mod1 & ~SFPXIADD_MOD1_IS_SUB) | old_sub));
    }
    break;
  case rvtt_insn_data::sfpxiadd_v:
    {
      int old_sub = get_int_arg(stmt, insnd->mod_pos) & SFPXIADD_MOD1_IS_SUB;
      gimple_call_set_arg(stmt, insnd->mod_pos,
			  build_int_cst(integer_type_node,
					(candidate_mod1 & ~SFPXIADD_MOD1_IS_SUB) | old_sub));
    }
    break;
  case rvtt_insn_data::sfpexexp:
    {
      int mod1 = ((candidate_mod1 & SFPXCMP_MOD1_CC_MASK) == SFPXCMP_MOD1_CC_LT) ?
	SFPEXEXP_MOD1_SET_CC_SGN_EXP : SFPEXEXP_MOD1_SET_CC_SGN_COMP_EXP;
      gimple_call_set_arg(stmt, insnd->mod_pos, build_int_cst(integer_type_node, mod1));
      break;
    }
  case rvtt_insn_data::sfpexexp_lv:
    {
      int mod1 = ((candidate_mod1 & SFPXCMP_MOD1_CC_MASK) == SFPXCMP_MOD1_CC_LT) ?
	SFPEXEXP_MOD1_SET_CC_SGN_EXP : SFPEXEXP_MOD1_SET_CC_SGN_COMP_EXP;
      gimple_call_set_arg(stmt, insnd->mod_pos, build_int_cst(integer_type_node, mod1));
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
    const rvtt_insn_data *insnd;
    if (rvtt_p (&insnd ,&stmt, gsi) && rvtt_sets_cc(insnd, stmt))
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

static void
fixup_vuse_vdef(gimple_stmt_iterator keep_gsi, gimple_stmt_iterator old_gsi)
{
  gimple *keep_g = gsi_stmt(keep_gsi);
  gimple *old_g = gsi_stmt(old_gsi);

  gimple_set_vuse(keep_g, gimple_vuse(old_g));
  gimple_set_vdef(keep_g, gimple_vdef(old_g));
  gimple_set_modified(keep_g, true);
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

// Combine sfpxiadd_i
//  - xiadd_i when used to set the CC but w/ no LHS combines with other CC
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
try_combine_sfpxiadd_i(const rvtt_insn_data *candidate_insnd,
		       gcall *candidate_stmt,
		       gimple_stmt_iterator candidate_gsi)
{
  bool combined = false;

  // Check for candidate iadd_i that sets the CC and compares to 0
  if (candidate_insnd->id == rvtt_insn_data::sfpxiadd_i &&
      is_int_arg(candidate_stmt, SFPXIADD_IMM_ARG_POS) && (get_int_arg(candidate_stmt, SFPXIADD_IMM_ARG_POS) == 0) &&
      ((get_int_arg(candidate_stmt, candidate_insnd->mod_pos) & SFPXCMP_MOD1_CC_MASK) != 0) &&
      gimple_call_lhs(candidate_stmt) == nullptr)
    {
      DUMP("Trying to combine %s\n", candidate_insnd->name);

      // Got a candidate
      int mod1 = get_int_arg(candidate_stmt, candidate_insnd->mod_pos);
      bool is_sign_bit_cc =
	((mod1 & SFPXCMP_MOD1_CC_MASK) == SFPXCMP_MOD1_CC_LT) ||
	((mod1 & SFPXCMP_MOD1_CC_MASK) == SFPXCMP_MOD1_CC_GTE);

      // Find when this variable was assigned
      gimple *assign_g = SSA_NAME_DEF_STMT(gimple_call_arg(candidate_stmt, SFPXIADD_SRC_ARG_POS));
      gcall *assign_stmt;
      const rvtt_insn_data *assign_insnd;

      if (rvtt_p(&assign_insnd, &assign_stmt, assign_g))
	{
	  gimple_stmt_iterator assign_gsi = gsi_for_stmt(assign_g);

	  if (gsi_bb(assign_gsi) == gsi_bb(candidate_gsi) &&
	      !intervening_cc_stmt(assign_gsi, candidate_gsi) &&
	      !intervening_use(gimple_call_lhs(assign_stmt), assign_gsi, candidate_gsi))
	    {
	      // Check to see if the assignment is one of the targeted optimizations
	      if (can_combine_sfpxiadd_i(assign_insnd, assign_stmt, is_sign_bit_cc))
		{
		  DUMP("	combining with %s\n", assign_insnd->name);

		  // Found a replaceable iadd_i
		  combine_sfpxiadd_i(assign_insnd, assign_stmt, candidate_insnd, candidate_stmt);

		  fixup_vuse_vdef(assign_gsi, candidate_gsi);

		  // Move target
		  gsi_move_before(&assign_gsi, &candidate_gsi);

		  // Remove candidate
		  rvtt_prep_stmt_for_deletion(candidate_stmt);

		  unlink_stmt_vdef(candidate_stmt);
		  gsi_remove(&candidate_gsi, true);
		  release_defs(candidate_stmt);

		  tree lhs = gimple_call_lhs(assign_stmt);
		  if (lhs != NULL_TREE && has_zero_uses(lhs))
		    {
		      DUMP("  lhs has zero uses, removing\n");
		      unlink_stmt_vdef(assign_stmt);
		      release_defs(assign_stmt);
		      gimple_call_set_lhs(assign_stmt, NULL_TREE);
		    }

		  update_stmt(assign_stmt);

		  combined = true;
		}
	    }
	}
    }

  return combined;
}

static inline bool
match_prior_assignment(rvtt_insn_data::insn_id id,
		       const rvtt_insn_data **prior_insnd,
		       gcall **prior_stmt,
		       gimple_stmt_iterator *prior_gsi,
		       tree src)
{
  *prior_gsi = gsi_for_stmt (SSA_NAME_DEF_STMT (src));
  if (!rvtt_p (prior_insnd, prior_stmt, *prior_gsi))
    return false;

  return (*prior_insnd)->id == id;
}

static inline void
validate_assumptions()
{
  gcc_assert(rvtt_insn_data::sfpmul + 2 == rvtt_insn_data::sfpmuli);
  gcc_assert(rvtt_insn_data::sfpadd + 2 == rvtt_insn_data::sfpaddi);
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
try_gen_muli_or_addi(const rvtt_insn_data *candidate_insnd,
		     gcall *candidate_stmt,
		     gimple_stmt_iterator candidate_gsi)
{
  bool combined = false;

  validate_assumptions();

  if (candidate_insnd->id == rvtt_insn_data::sfpmul ||
      candidate_insnd->id == rvtt_insn_data::sfpmul_lv ||
      candidate_insnd->id == rvtt_insn_data::sfpadd ||
      candidate_insnd->id == rvtt_insn_data::sfpadd_lv)
    {
      DUMP("Trying to combine %s into %si\n", candidate_insnd->name,
	   rvtt_get_notlive_version(candidate_insnd)->name);

      int live = candidate_insnd->live_p();
      gimple_stmt_iterator assign_gsi;
      gcall *assign_stmt;
      const rvtt_insn_data *assign_insnd;
      int which_arg = 0;
      tree value;

      // Only combine live if we are writing to the same arg as the dst arg
      bool found_one = (match_prior_assignment(rvtt_insn_data::sfpxloadi,
					       &assign_insnd, &assign_stmt, &assign_gsi,
					       gimple_call_arg(candidate_stmt, which_arg + live)) &&
			!subsequent_use(gimple_call_arg(candidate_stmt, (which_arg ^ 1) + live), candidate_gsi) &&
			gsi_bb(assign_gsi) == gsi_bb(candidate_gsi) &&
			!intervening_cc_stmt(assign_gsi, candidate_gsi) &&
			(!live || gimple_call_arg(candidate_stmt, 0) == gimple_call_arg(candidate_stmt, (which_arg ^ 1) + live)) &&
			rvtt_get_fp16b(&value, assign_stmt, assign_insnd));

      if (!found_one)
	{
	  which_arg = 1;
	  found_one = (match_prior_assignment(rvtt_insn_data::sfpxloadi,
					      &assign_insnd, &assign_stmt, &assign_gsi,
					      gimple_call_arg(candidate_stmt, which_arg + live)) &&
		       !subsequent_use(gimple_call_arg(candidate_stmt, (which_arg ^ 1) + live), candidate_gsi) &&
		       gsi_bb(assign_gsi) == gsi_bb(candidate_gsi) &&
		       !intervening_cc_stmt(assign_gsi, candidate_gsi) &&
		       (!live || gimple_call_arg(candidate_stmt, 0) == gimple_call_arg(candidate_stmt, (which_arg ^ 1) + live)) &&
		       rvtt_get_fp16b(&value, assign_stmt, assign_insnd));
	}

      if (found_one)
	{
	  DUMP("  found a matching %s...\n", assign_insnd->name);

	  const rvtt_insn_data *opi_insnd = rvtt_get_notlive_version(candidate_insnd) + 2;
	  DUMP("  combining %s arg %d w/ loadi into %s\n", candidate_insnd->name, which_arg, opi_insnd->name);

	  // Create <add,mul>i
	  // addi/muli are "implicitly live" (dst_as_src), no explicit live versions
	  gcall* opi_stmt = gimple_build_call(opi_insnd->decl, 6);
	  gimple_call_set_arg(opi_stmt, 0, gimple_call_arg(assign_stmt, 0));
	  gimple_call_set_arg(opi_stmt, 1, gimple_call_arg(candidate_stmt, live + (which_arg ^ 1)));
	  gimple_call_set_arg(opi_stmt, 2, value);
	  gimple_call_set_arg(opi_stmt, 5, build_int_cst(integer_type_node,
							 get_int_arg(candidate_stmt, candidate_insnd->mod_pos)));

	  if (TREE_CODE(value) == SSA_NAME)
	    {
	      // Have an fp16b as an non-immediate value
	      // 2 issues to worry about:
	      //  - the loadi shft/mask is different from the addi/muli shft/mask
	      //  - loop unrolling may create multiple related uses
	      // Issue new nonimm-prologue for the addi, track to see if it can be re-used
	      tree old_add = gimple_call_arg(assign_stmt, assign_insnd->nonimm_pos + 1);
	      int unique_id = get_int_arg(assign_stmt, assign_insnd->nonimm_pos + 2);
	      gcc_assert((unique_id & 1) == 0);
	      rvtt_link_nonimm_prologue(load_imm_map, unique_id + 1, old_add, opi_insnd, opi_stmt);
	    }
	  else
	    {
	      gimple_call_set_arg(opi_stmt, 3, build_int_cst(integer_type_node, 0));
	      gimple_call_set_arg(opi_stmt, 4, build_int_cst(integer_type_node, 0));
	    }

	  gimple_call_set_lhs(opi_stmt, gimple_call_lhs(candidate_stmt));
	  gimple_set_location(opi_stmt, gimple_location (candidate_stmt));
	  update_stmt(opi_stmt);
	  gsi_insert_before(&candidate_gsi, opi_stmt, GSI_SAME_STMT);

	  // Delete op
	  unlink_stmt_vdef(candidate_stmt);
	  gsi_remove(&candidate_gsi, true);

	  combined = true;
	}
    }

  return combined;
}

static bool
remove_unused_loadis(basic_block bb)
{
  DUMP("Checking for unused loadi(s)\n");

  bool removed = false;
  gimple_stmt_iterator gsi;

  gsi = gsi_start_bb(bb);
  while (!gsi_end_p(gsi))
    {
      gcall *stmt;
      const rvtt_insn_data *insnd;
      if (rvtt_p(&insnd, &stmt, gsi))
	{
	  tree lhs = gimple_call_lhs(stmt);
	  if (insnd->id == rvtt_insn_data::sfpxloadi &&
	      (lhs == nullptr || has_zero_uses(lhs)))
	    {
	      DUMP("  removing %s %p %p\n", insnd->name, stmt, lhs);

	      // Remove candidate
	      rvtt_prep_stmt_for_deletion(stmt);

	      unlink_stmt_vdef(stmt);
	      gsi_remove(&gsi, true);
	      release_defs(stmt);

	      removed = true;
	    }
	}
      if (!gsi_end_p(gsi))
	gsi_next (&gsi);
    }

  return removed;
}

struct probe_t {
  gimple_stmt_iterator gsi;
  const rvtt_insn_data *insnd = nullptr;
  gcall *call = nullptr;

  probe_t () = default;
  probe_t (gimple_stmt_iterator it) : gsi (it), insnd (nullptr), call (nullptr) {}

  operator bool () const
  {
    return bool (call);
  }
};

/* Look for add (mul (a, b), c) and turn it into mad (a, b, c).
   We have already handled negated inputs on BlackHole.  */

static bool
try_combine_mul_add (probe_t &probe)
{
  if (!(probe.insnd->id == rvtt_insn_data::sfpadd || probe.insnd->id == rvtt_insn_data::sfpadd_lv))
    return false;

  bool is_lv = probe.insnd->live_p ();
  int add_mod = get_int_arg (probe.call, probe.insnd->mod_pos);

  probe_t mul;
  int mul_op_no = -1;
  for (int op_no = 2; op_no--;)
    {
      tree operand = gimple_call_arg (probe.call, op_no + is_lv);
      if (!has_single_use (operand))
	continue;

      mul = gsi_for_stmt (SSA_NAME_DEF_STMT (operand));
      if (!rvtt_p (&mul.insnd, &mul.call, mul.gsi))
	continue;

      // We can't handle sfpmul_lv here, because that won't propagate
      // through the sfpmuladd_lv we generate -- that takes the live
      // value from the sfpadd_lv.
      bool is_mul = mul.insnd->id == rvtt_insn_data::sfpmul;
      if (!is_mul)
	continue;

      if (intervening_cc_stmt (mul.gsi, probe.gsi))
	continue;

      mul_op_no = op_no;
      break;
    }

  if (mul_op_no < 0)
    return false;

  int mul_mod = get_int_arg (mul.call, mul.insnd->mod_pos);

  int muladd_mod = add_mod ^ mul_mod;

  const rvtt_insn_data *muladd_insnd
    = rvtt_get_insn_data (is_lv ? rvtt_insn_data::sfpmad_lv : rvtt_insn_data::sfpmad);
  gcall *muladd_call = gimple_build_call (muladd_insnd->decl, 4 + is_lv);
  if (is_lv)
    gimple_call_set_arg (muladd_call, 0, gimple_call_arg (probe.call, 0));
  gimple_call_set_arg (muladd_call, is_lv + 0, gimple_call_arg (mul.call, 0));
  gimple_call_set_arg (muladd_call, is_lv + 1, gimple_call_arg (mul.call, 1));
  gimple_call_set_arg (muladd_call, is_lv + 2, gimple_call_arg (probe.call, (1 - mul_op_no) + is_lv));
  gimple_call_set_arg (muladd_call, muladd_insnd->mod_pos, build_int_cst (integer_type_node, muladd_mod));
  gimple_call_set_lhs (muladd_call, gimple_call_lhs (probe.call));
  gimple_set_location (muladd_call, gimple_location (probe.call));

  update_stmt (muladd_call);
  gsi_insert_after (&probe.gsi, muladd_call, GSI_SAME_STMT);

  // Remove the add
  gsi_remove (&probe.gsi, true);

  // Remove the mul
  unlink_stmt_vdef (mul.call);
  gsi_remove (&mul.gsi, true);
  release_defs (mul.call);

  return true;
}

/* Returns true if this multiply is actually a negation (a multiply by
   -1)  */

static bool
is_neg_1 (tree operand)
{
  probe_t assign;
  if (!match_prior_assignment (rvtt_insn_data::sfpreadlreg,
			       &assign.insnd, &assign.call, &assign.gsi, operand))
    return false;

  return get_int_arg (assign.call, 0) == CREG_IDX_NEG_1;
}

/* Returns true if this multiply is actually a negation (a multiply by
   -1)  */

static bool
is_negation (probe_t &probe)
{
  if (probe.insnd->id == rvtt_insn_data::sfpmul || probe.insnd->id == rvtt_insn_data::sfpmul_lv)
    {
      bool is_lv = probe.insnd->live_p ();
      tree operand = gimple_call_arg (probe.call, 1 + is_lv);
      return is_neg_1 (operand);
    }

  if (probe.insnd->id == rvtt_insn_data::sfpmov || probe.insnd->id == rvtt_insn_data::sfpmov_lv)
    return get_int_arg (probe.call, probe.insnd->mod_pos) == SFPMOV_MOD1_COMPL;

  return false;
}

/* Look for adds and muls with negated inputs, and merge the negation
   into the add or mul's mod1 operand.  */

static bool
try_combine_negated_operands (probe_t &probe)
{
  gcc_assert (TARGET_XTT_TENSIX_BH);
  
  bool is_add = probe.insnd->id == rvtt_insn_data::sfpadd || probe.insnd->id == rvtt_insn_data::sfpadd_lv;
  bool is_mul = probe.insnd->id == rvtt_insn_data::sfpmul || probe.insnd->id == rvtt_insn_data::sfpmul_lv;
  bool is_mad = probe.insnd->id == rvtt_insn_data::sfpmad || probe.insnd->id == rvtt_insn_data::sfpmad_lv;

  if (!(is_add || is_mul || is_mad))
    return false;

  bool is_lv = probe.insnd->live_p ();
  bool result = false;
  for (unsigned op = 2 + is_mad; op--;)
    {
      tree operand = gimple_call_arg (probe.call, op + is_lv);
      if (!has_single_use (operand))
	continue;

      probe_t input = gsi_for_stmt (SSA_NAME_DEF_STMT (operand));
      if (!rvtt_p (&input.insnd, &input.call, input.gsi))
	continue;

      if (!is_negation (input))
	continue;

      if (intervening_cc_stmt (input.gsi, probe.gsi))
	continue;

      gcc_assert (!is_lv || gimple_call_arg (probe.call, 0) != gimple_call_lhs (input.call));

      if (TARGET_XTT_TENSIX_BH)
	{
	  // Elide the negation, and invert the appropriate mod1 bit
	  gimple_call_set_arg (probe.call, op + is_lv, gimple_call_arg (input.call, input.insnd->live_p ()));
	  int mod = get_int_arg (probe.call, probe.insnd->mod_pos);
	  mod ^= !op || is_mul || (is_mad && op == 1) ? SFPMAD_MOD1_BH_COMPL_A : SFPMAD_MOD1_BH_COMPL_C;
	  gimple_call_set_arg (probe.call, probe.insnd->mod_pos, build_int_cst (integer_type_node, mod));

	  update_stmt (probe.call);
	}
      else
	{
	  // Replace the add with a muladd multiplying the appropriate
	  // operand by -1.
	  const rvtt_insn_data *lreg_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);
	  gcall *lreg_call = gimple_build_call (lreg_insnd->decl, 1);
	  gimple_call_set_return_slot_opt (lreg_call, true);
	  gimple_set_location (lreg_call, gimple_location (probe.call));
	  gimple_call_set_arg (lreg_call, 0, build_int_cst (integer_type_node, CREG_IDX_NEG_1));
	  tree type = TREE_TYPE (TREE_TYPE (probe.insnd->decl));
	  tree ssa = make_ssa_name (type, lreg_call);
	  gimple_call_set_lhs (lreg_call, ssa);
	  gsi_insert_before (&probe.gsi, lreg_call, GSI_SAME_STMT);

	  // sfpmad (maybe-live, a, b, c, mod)
	  const rvtt_insn_data *muladd_insnd
	    = rvtt_get_insn_data (is_lv ? rvtt_insn_data::sfpmad_lv : rvtt_insn_data::sfpmad);
	  gcall *muladd_call = gimple_build_call (muladd_insnd->decl, 4 + is_lv);
	  if (is_lv)
	    gimple_call_set_arg (muladd_call, 0, gimple_call_arg (probe.call, 0));
	  gimple_call_set_arg (muladd_call, is_lv + 0, ssa);
	  gimple_call_set_arg (muladd_call, is_lv + 1, gimple_call_arg (input.call, input.insnd->live_p ()));
	  gimple_call_set_arg (muladd_call, is_lv + 2, gimple_call_arg (probe.call, (1 - op) + is_lv));
	  gimple_call_set_arg (muladd_call, muladd_insnd->mod_pos, build_int_cst (integer_type_node, 0));
	  gimple_call_set_lhs (muladd_call, gimple_call_lhs (probe.call));
	  gimple_set_location (muladd_call, gimple_location (probe.call));

	  update_stmt (muladd_call);
	  gsi_insert_after (&probe.gsi, muladd_call, GSI_SAME_STMT);
	  gsi_remove (&probe.gsi, true); // GSI will now be at the
					 // added MAD
	  // Inform our caller that the insn changed.
	  probe.insnd = muladd_insnd;
	}
      unlink_stmt_vdef (input.call);
      gsi_remove (&input.gsi, true);
      release_defs (input.call);

      result = true;
      if (!TARGET_XTT_TENSIX_BH)
	break;
    }
  return result;
}

/* Look for negations of adds, muls, muladds or negations.  */

static bool
try_combine_negated_result (probe_t &probe)
{
  gcc_assert (TARGET_XTT_TENSIX_BH);

  if (!is_negation (probe))
    return false;

  bool is_lv = probe.insnd->live_p ();
  tree operand = gimple_call_arg (probe.call, is_lv);
  if (!has_single_use (operand))
    return false;

  gimple *assign = SSA_NAME_DEF_STMT (operand);
  probe_t input = gsi_for_stmt (assign);
  if (!rvtt_p (&input.insnd, &input.call, input.gsi))
    return false;

  if (intervening_cc_stmt (input.gsi, probe.gsi))
    return false;

  bool is_add = input.insnd->id == rvtt_insn_data::sfpadd || input.insnd->id == rvtt_insn_data::sfpadd_lv;
  bool is_mul = input.insnd->id == rvtt_insn_data::sfpmul || input.insnd->id == rvtt_insn_data::sfpmul_lv;
  bool is_muladd = input.insnd->id == rvtt_insn_data::sfpmad || input.insnd->id == rvtt_insn_data::sfpmad_lv;

  if (!(is_add || is_mul || is_muladd))
    return false;

  gcc_assert (!is_lv || gimple_call_arg (probe.call, 0) == gimple_call_lhs (input.call));

  // Invert the appropriate mod1 bits
  int mod = get_int_arg (input.call, input.insnd->mod_pos);
  mod ^= SFPMAD_MOD1_BH_COMPL_A | (is_mul ? 0 : SFPMAD_MOD1_BH_COMPL_C);
  gimple_call_set_arg (input.call, input.insnd->mod_pos, build_int_cst (integer_type_node, mod));
  release_ssa_name (gimple_call_lhs (input.call));
  gimple_call_set_lhs (input.call, gimple_call_lhs (probe.call));

  gsi_remove (&probe.gsi, true);
  unlink_stmt_vdef (probe.call);
  gsi_prev (&probe.gsi);

  return true;
}

static bool
try_combine_negated_add_operand (probe_t &probe)
{
  gcc_assert (!TARGET_XTT_TENSIX_BH);

  bool is_add = probe.insnd->id == rvtt_insn_data::sfpadd || probe.insnd->id == rvtt_insn_data::sfpadd_lv;
  if (!is_add)
    return false;

  bool is_lv = probe.insnd->live_p ();
  for (unsigned op = 2; op--;)
    {
      tree operand = gimple_call_arg (probe.call, op + is_lv);
      if (!has_single_use (operand))
	continue;

      probe_t input = gsi_for_stmt (SSA_NAME_DEF_STMT (operand));
      if (!rvtt_p (&input.insnd, &input.call, input.gsi))
	continue;

      if (!is_negation (input))
	continue;

      if (intervening_cc_stmt (input.gsi, probe.gsi))
	continue;

      gcc_assert (!is_lv || gimple_call_arg (probe.call, 0) != gimple_call_lhs (input.call));

      // Replace the add with a muladd multiplying the appropriate
      // operand by -1.
      const rvtt_insn_data *lreg_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);
      gcall *lreg_call = gimple_build_call (lreg_insnd->decl, 1);
      gimple_call_set_return_slot_opt (lreg_call, true);
      gimple_set_location (lreg_call, gimple_location (probe.call));
      gimple_call_set_arg (lreg_call, 0, build_int_cst (integer_type_node, CREG_IDX_NEG_1));
      tree type = TREE_TYPE (TREE_TYPE (probe.insnd->decl));
      tree ssa = make_ssa_name (type, lreg_call);
      gimple_call_set_lhs (lreg_call, ssa);
      gsi_insert_before (&probe.gsi, lreg_call, GSI_SAME_STMT);

      // sfpmad (maybe-live, a, b, c, mod)
      const rvtt_insn_data *muladd_insnd
	= rvtt_get_insn_data (is_lv ? rvtt_insn_data::sfpmad_lv : rvtt_insn_data::sfpmad);
      gcall *muladd_call = gimple_build_call (muladd_insnd->decl, 4 + is_lv);
      if (is_lv)
	gimple_call_set_arg (muladd_call, 0, gimple_call_arg (probe.call, 0));
      gimple_call_set_arg (muladd_call, is_lv + 0, ssa);
      gimple_call_set_arg (muladd_call, is_lv + 1, gimple_call_arg (input.call, input.insnd->live_p ()));
      gimple_call_set_arg (muladd_call, is_lv + 2, gimple_call_arg (probe.call, (1 - op) + is_lv));
      gimple_call_set_arg (muladd_call, muladd_insnd->mod_pos, build_int_cst (integer_type_node, 0));
      gimple_call_set_lhs (muladd_call, gimple_call_lhs (probe.call));
      gimple_set_location (muladd_call, gimple_location (probe.call));

      update_stmt (muladd_call);
      gsi_insert_after (&probe.gsi, muladd_call, GSI_SAME_STMT);
      gsi_remove (&probe.gsi, true); // GSI will now be at the added MAD

      unlink_stmt_vdef (input.call);
      gsi_remove (&input.gsi, true);
      release_defs (input.call);

      return true;
    }

  return false;
}

// Optimize stmt sequence by combining builtins.  Handles:
//   - add/mul w/ one operand a loadi of .5, use mod to handle .5
//   - sfpxiadd_i w/ sfpxiadd_i, exexp
//   - add w/ mul to form mad
//   - mul or add w/ loadi to make muli/addi
// The order of the half/mad/addi+muli matter, the progression of this
// ordering eliminates the need, for example, of turning a mad w/ the add of
// .5 into a mul then trying to recombine into a mad w/ a subsequent add
static void
transform (function *fun)
{
  basic_block bb;
  load_imm_map.reserve (20);

  // Pass one: combine iadd
  FOR_EACH_BB_FN (bb, fun)
    {
      bool update = false;

      for (gimple_stmt_iterator candidate_gsi = gsi_start_bb (bb);
	   !gsi_end_p (candidate_gsi); gsi_next (&candidate_gsi))
	{
	  gcall *candidate_stmt;
	  const rvtt_insn_data *candidate_insnd;

	  if (rvtt_p (&candidate_insnd, &candidate_stmt, candidate_gsi))
	    {
	      update |= try_combine_sfpxiadd_i (candidate_insnd, candidate_stmt, candidate_gsi);
	    }
	}

      if (update)
	update_ssa(TODO_update_ssa);

      update = false;
      for (gimple_stmt_iterator candidate_gsi = gsi_start_bb (bb);
	   !gsi_end_p (candidate_gsi); gsi_next (&candidate_gsi))
	{
	  gcall *candidate_stmt;
	  const rvtt_insn_data *candidate_insnd;

	  if (rvtt_p (&candidate_insnd, &candidate_stmt, candidate_gsi))
	    update |= try_gen_muli_or_addi(candidate_insnd, candidate_stmt, candidate_gsi);
	}

      update |= remove_unused_loadis (bb);
      if (update)
	update_ssa(TODO_update_ssa);

      update = false;
      for (probe_t probe (gsi_start_bb (bb));
	   !gsi_end_p (probe.gsi); gsi_next (&probe.gsi))
	{
	  if (!rvtt_p (&probe.insnd, &probe.call, probe.gsi))
	    continue;

	  // FIXME: Look for paired negations
	  if (TARGET_XTT_TENSIX_BH)
	    {
	      if (try_combine_negated_operands (probe))
		update = true;
	      else if (try_combine_negated_result (probe))
		update = true;
	    }

	  if (try_combine_mul_add (probe))
	    update = true;
	  else if (!TARGET_XTT_TENSIX_BH &&
		   try_combine_negated_add_operand (probe))
	    update = true;
	}

      if (update)
	update_ssa(TODO_update_ssa);
    }

  load_imm_map.clear ();
}

namespace {

const pass_data pass_data_rvtt_combine =
{
  GIMPLE_PASS, /* type */
  "rvtt_combine", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_combine : public gimple_opt_pass
{
public:
  pass_rvtt_combine (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_combine, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX_OPT_COMBINE;
  }

  virtual unsigned execute (function *fn) override
  {
    transform (fn);
    return 0;
  }
}; // class pass_rvtt_combine

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_combine (gcc::context *ctxt)
{
  return new pass_rvtt_combine (ctxt);
}
