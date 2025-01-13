/* Pass to expand (lower) boolean SFPU operators
   Copyright (C) 2022 Free Software Foundation, Inc.
   Contributed by Paul Keller (pkeller@tenstorrent.com).

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
#include <unordered_map>
#include "rvtt.h"

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

using namespace std;

static void process_tree(gcall *stmt, gcall *parent);
static void process_tree_node(gimple_stmt_iterator *pre_gsip, gimple_stmt_iterator *post_gsip,
			      bool *negated, gcall *stmt, gcall *parent, bool negate);

static unordered_map<gcall *, bool> vif_stmts;
static unordered_map<gcall *, bool> phi_stmts;

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
remove_stmt(gimple *g)
{
  rvtt_prep_stmt_for_deletion(g);
  unlink_stmt_vdef(g);
  gimple_stmt_iterator gsi = gsi_for_stmt(g);
  gsi_remove(&gsi, true);
  release_defs(g);
}

static int
negate_cmp_mod(int mod)
{
    int op = mod & SFPXCMP_MOD1_CC_MASK;
    int new_op = 0;

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
    default:
      gcc_unreachable();
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
flip_negated_cmp(gcall *stmt, const rvtt_insn_data *insnd, bool negate)
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

// Expand icmp[sv] intrinsics into the appropriate subtract
// sequence. Ordering compares are more complicated than usual, as we
// must use subtract and it doesn't expose overflow or carry flags.

static void
expand_icmp (gcall *icmp, const rvtt_insn_data *insnd, gimple_stmt_iterator *begin, gimple_stmt_iterator *end)
{
  *end = gsi_for_stmt (icmp);
  *begin = gsi_none ();

  bool is_const = insnd->id == rvtt_insn_data::sfpxicmps;
  gcc_assert (is_const || insnd->id == rvtt_insn_data::sfpxicmpv);

  // icmps has an initial pointer argument for the (now determined
  // unnecessary) instruction synth. (it also has 2 related integer
  // arguments between the two compared args and the mod arg).
  tree op_a = gimple_call_arg (icmp, 0 + int (is_const));
  tree op_b = gimple_call_arg (icmp, 1 + int (is_const));
  gcc_assert (is_const ? TREE_CODE (op_b) == INTEGER_CST : SSA_VAR_P (op_b));

  gcc_assert (insnd->mod_pos + 1u == gimple_call_num_args (icmp));
  int mod = get_int_arg (icmp, insnd->mod_pos);
  int cmp = mod & SFPXCMP_MOD1_CC_MASK;
  bool is_signed = bool (mod & SFPXIADD_MOD1_SIGNED);
  auto iadd_code = is_const ? rvtt_insn_data::sfpxiadd_i : rvtt_insn_data::sfpxiadd_v;

  if (cmp == SFPXCMP_MOD1_CC_EQ
      || cmp == SFPXCMP_MOD1_CC_NE
      // We used to erroneously handle all compares like
      // this. Grayskull has insufficient registers to do it right in
      // all but the simplest of programs.
      || flag_tt_incorrect_ordering_icmp) {
    // Equality/inequalitycompare, generate an sfpxiadd
    const rvtt_insn_data *new_insnd = rvtt_get_insn_data (iadd_code);
    int nargs = gimple_call_num_args (icmp);
    gcall *add = gimple_build_call (new_insnd->decl, nargs);
    gimple_set_location (add, gimple_location (icmp));

    for (int i = 0; i < nargs; i++)
      gimple_call_set_arg (add, i, gimple_call_arg (icmp, i));

    // It's a subtract whose result is ignored.
    int add_mod = mod | SFPXIADD_MOD1_IS_SUB | SFPXIADD_MOD1_DST_UNUSED;
    gimple_call_set_arg (add, insnd->mod_pos,
			 build_int_cst (integer_type_node, add_mod));

    gimple_call_set_lhs (add, NULL_TREE);
    gimple_set_vuse (add, gimple_vuse(icmp));

    gsi_insert_after (end, add, GSI_NEW_STMT);
    *begin = *end;

    remove_stmt (icmp);
    return;
  }

  // Ordering compare. A subtract is insufficient, because we have to
  // deal with overflow. We need to check whether the two operands are
  // in the same half of the number space, so overflow won't occur.
  // When they are in different halves, the result is a fixed true, or
  // a fixed false.

  //                    cond-is   same-sign    diff-sign
  //  signed a gte b     ~sign      a - b      a
  //unsigned a gte b     ~sign      a - b      b
  //  signed a lt  b      sign      a - b      a
  //unsigned a lt  b      sign      a - b      b

  //  signed a lte b     ~sign      b - a      b
  //unsigned a lte b     ~sign      b - a      a
  //  signed a gt  b      sign      b - a      b
  //unsigned a gt  b      sign      b - a      a

  //  signed a lte cst    sign      a - cst+1  a
  //unsigned a lte cst    sign      a - cst+1  cst+1
  //  signed a gt  cst   ~sign      a - cst+1  a
  //unsigned a gt  cst   ~sign      a - cst+1  cst+1

  bool cond_is_inverted = false;
  bool inc_const = false;
  bool swap_ops = false;
  bool use_a_msb = false;

  switch (cmp)
    {
    default:
      gcc_unreachable ();

    case SFPXCMP_MOD1_CC_GTE:
      cond_is_inverted = true;
      [[fallthrough]];

    case SFPXCMP_MOD1_CC_LT:
      use_a_msb = is_signed;
      break;

    case SFPXCMP_MOD1_CC_LTE:
      cond_is_inverted = true;
      [[fallthrough]];

    case SFPXCMP_MOD1_CC_GT:
      if (is_const)
	{
	  inc_const = true;
	  cond_is_inverted = !cond_is_inverted;
	  use_a_msb = is_signed;
	}
      else
	{
	  swap_ops = true;
	  use_a_msb = !is_signed;
	}
      break;
    }

  // xor = sfpxor (a, b)
  // sfpsetcc_v (xor, 4) // set CC from ~MSB
  // res = sfpmov (b, 2) // copy all of b
  // res_a = sfpmov (a, 0) // copy enabled bits of a
  // res = sfpassign (res, res_a)
  // res = sfpiadd (res, b, 0, 6) // subtract b
  // sfpencc 3, 10 // get TOS?
  // sfpsetcc_v res, 0 // set cc from MSB
  tree type = TREE_TYPE (op_a);

  tree ssa_var = NULL_TREE;
  tree ssa_msb = NULL_TREE;
  bool const_msb_set = false;

  if (is_const)
    {
      unsigned HOST_WIDE_INT val = TREE_INT_CST_LOW (op_b);
      if (inc_const)
	{
	  val = (val + 1) & 0xffffffff;
	  op_b = build_int_cst (TREE_TYPE (op_b), val);
	  // FIXME: What if this wraps from mostpos? Surely in that
	  // case the result of the compare is already known? (fixed
	  // true or fixed false)
	  gcc_assert (val != 0x80000000);
	}
      const_msb_set = bool (val & (1ul << 31));
      ssa_var = op_a;
      if (!use_a_msb)
	{
	  const rvtt_insn_data *encc_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpencc);
	  gcall *encc_call = gimple_build_call (encc_insnd->decl, 2);
	  gimple_set_location (encc_call, gimple_location (icmp));
	  gimple_call_set_arg (encc_call, 0, build_int_cst (integer_type_node, 3));
	  gimple_call_set_arg (encc_call, 1, build_int_cst (integer_type_node, 10));
	  gsi_insert_after (end, encc_call, GSI_NEW_STMT);
	  *begin = *end;

	  const rvtt_insn_data *loadi_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpxloadi);
	  gcall *loadi_call = gimple_build_call (loadi_insnd->decl, 5);
	  gimple_set_location (loadi_call, gimple_location (icmp));
	  gimple_call_set_arg (loadi_call, 0, integer_zero_node);
	  gimple_call_set_arg (loadi_call, 2, build_int_cst (integer_type_node, val >> 16));
	  gimple_call_set_arg (loadi_call, loadi_insnd->mod_pos,
			       build_int_cst (integer_type_node, SFPLOADI_MOD0_SHORT));
	  gimple_call_set_arg (loadi_call, 3, integer_zero_node);
	  gimple_call_set_arg (loadi_call, 4, integer_zero_node);
	  ssa_msb = make_ssa_name (type, loadi_call);
	  gimple_call_set_lhs (loadi_call, ssa_msb);
	  gsi_insert_after (end, loadi_call, GSI_NEW_STMT);
	}
    }
  else
    {
      // op_a XOR op_b
      const rvtt_insn_data *xor_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpxor);
      if (xor_insnd->decl)
	{
	  gcall *xor_call = gimple_build_call (xor_insnd->decl, 2);
	  gimple_set_location (xor_call, gimple_location (icmp));
	  gimple_call_set_arg (xor_call, 0, op_a);
	  gimple_call_set_arg (xor_call, 1, op_b);
	  ssa_var = make_ssa_name (type, xor_call);
	  gimple_call_set_lhs (xor_call, ssa_var);
	  gsi_insert_after (end, xor_call, GSI_NEW_STMT);
	  *begin = *end;
	}
      else
	{
	  // Grayskull has no xor insn, need to synthesize it
	  // xor: and(or, not(and))
	  gcc_assert (TARGET_RVTT_GS);
	  const rvtt_insn_data *or_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpor);
	  gcall *or_call = gimple_build_call (or_insnd->decl, 2);
	  gimple_set_location (or_call, gimple_location (icmp));
	  gimple_call_set_arg (or_call, 0, op_a);
	  gimple_call_set_arg (or_call, 1, op_b);
	  tree or_var = make_ssa_name (type, or_call);
	  gimple_call_set_lhs (or_call, or_var);
	  gsi_insert_after (end, or_call, GSI_NEW_STMT);
	  *begin = *end;

	  const rvtt_insn_data *and_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpand);
	  gcall *and_call = gimple_build_call (and_insnd->decl, 2);
	  gimple_set_location (and_call, gimple_location (icmp));
	  gimple_call_set_arg (and_call, 0, op_a);
	  gimple_call_set_arg (and_call, 1, op_b);
	  tree and_var = make_ssa_name (type, and_call);
	  gimple_call_set_lhs (and_call, and_var);
	  gsi_insert_after (end, and_call, GSI_NEW_STMT);

	  const rvtt_insn_data *not_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpnot);
	  gcall *not_call = gimple_build_call (not_insnd->decl, 1);
	  gimple_set_location (not_call, gimple_location (icmp));
	  gimple_call_set_arg (not_call, 0, and_var);
	  tree not_var = make_ssa_name (type, not_call);
	  gimple_call_set_lhs (not_call, not_var);
	  gsi_insert_after (end, not_call, GSI_NEW_STMT);

	  and_call = gimple_build_call (and_insnd->decl, 2);
	  gimple_set_location (and_call, gimple_location (icmp));
	  gimple_call_set_arg (and_call, 0, or_var);
	  gimple_call_set_arg (and_call, 1, not_var);
	  ssa_var = make_ssa_name (type, and_call);
	  gimple_call_set_lhs (and_call, ssa_var);
	  gsi_insert_after (end, and_call, GSI_NEW_STMT);
	}
  }

  // set CC
  const rvtt_insn_data *setcc_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpsetcc_v);
  gcall *setcc_call = gimple_build_call (setcc_insnd->decl, 2);
  gimple_set_location (setcc_call, gimple_location (icmp));
  gimple_call_set_arg (setcc_call, 0, ssa_var);
  // 4: set cc to complement of MSB
  // 0: set cc to MSB
  bool need_gs_flip = TARGET_RVTT_GS && (use_a_msb || !is_const);
  gimple_call_set_arg (setcc_call, 1, build_int_cst (integer_type_node,
						     const_msb_set ^ need_gs_flip ? 0 : 4));
  gsi_insert_after (end, setcc_call, GSI_NEW_STMT);
  if (gsi_end_p (*begin))
    *begin = *end;

  if (use_a_msb || !is_const)
    {
      // copy use_a_msb ? a : b to res
      const rvtt_insn_data *mov_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpmov);
      gcall *mov1_call = gimple_build_call (mov_insnd->decl, 2);
      gimple_set_location (mov1_call, gimple_location (icmp));
      gimple_call_set_arg (mov1_call, 0, use_a_msb ? op_a : op_b);
      // Copy all of src, ignoring CC (not possible on GS)
      gimple_call_set_arg (mov1_call, 1, build_int_cst (integer_type_node, TARGET_RVTT_GS ? 0 : 2));
      ssa_var = make_ssa_name (type, mov1_call);
      gimple_call_set_lhs (mov1_call, ssa_var);
      gsi_insert_after (end, mov1_call, GSI_NEW_STMT);

      if (TARGET_RVTT_GS)
	{
	  gcc_assert (need_gs_flip);
	  // Invert enable bits.
	  const rvtt_insn_data *compc_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpcompc);
	  gcall *compc_call = gimple_build_call (compc_insnd->decl, 0);
	  gimple_set_location (compc_call, gimple_location (icmp));
	  gsi_insert_after (end, compc_call, GSI_NEW_STMT);
	}
    }
  else if (!use_a_msb)
    {
      gcc_assert (ssa_msb);
      ssa_var = ssa_msb;
    }

  if (use_a_msb == swap_ops || TARGET_RVTT_GS)
    {
      // copy swap_ops ? b : a
      const rvtt_insn_data *mov_insnd = rvtt_get_insn_data (rvtt_insn_data::sfpmov_lv);
      gcall *mov2_call = gimple_build_call (mov_insnd->decl, 3);
      gimple_set_location (mov2_call, gimple_location (icmp));
      gimple_call_set_arg (mov2_call, 0, ssa_var);
      gimple_call_set_arg (mov2_call, 1, swap_ops ? op_b : op_a);
      // Copy CC-enabled lanes
      gimple_call_set_arg (mov2_call, 2, integer_zero_node);

      ssa_var = make_ssa_name (type, mov2_call);
      gimple_call_set_lhs (mov2_call, ssa_var);

      gsi_insert_after (end, mov2_call, GSI_NEW_STMT);
    }

  const rvtt_insn_data *iadd_insnd = rvtt_get_insn_data (iadd_code);
  int nargs = gimple_call_num_args (icmp);
  gcall *iadd_call = gimple_build_call (iadd_insnd->decl, nargs);
  gimple_set_location (iadd_call, gimple_location (icmp));

  for (int i = 0; i < nargs; i++)
    gimple_call_set_arg (iadd_call, i, gimple_call_arg (icmp, i));
  gimple_call_set_arg (iadd_call, 0 + int (is_const), ssa_var);
  gimple_call_set_arg (iadd_call, 1 + int (is_const), swap_ops ? op_a : op_b);
  gimple_call_set_arg (iadd_call, iadd_insnd->mod_pos,
		       build_int_cst (integer_type_node, SFPXIADD_MOD1_IS_SUB));
  ssa_var = make_ssa_name (type, iadd_call);
  gimple_call_set_lhs (iadd_call, ssa_var);
  gsi_insert_after (end, iadd_call, GSI_NEW_STMT);

  const rvtt_insn_data *popc_insnd = rvtt_get_insn_data (rvtt_insn_data::sfppopc);
  // Grayskull has no mod
  bool has_mod = popc_insnd->mod_pos >= 0;
  gcall *popc_call = gimple_build_call (popc_insnd->decl, has_mod ? 1 : 0);
  gimple_set_location (popc_call, gimple_location (icmp));
  if (has_mod)
    // 1: preserve TOS
    gimple_call_set_arg (popc_call, 0, build_int_cst (integer_type_node, SFPPOPCC_MOD1_COPY));
  gsi_insert_after (end, popc_call, GSI_NEW_STMT);
  if (!has_mod)
    {
      // Push it back.
      const rvtt_insn_data *pushc_insnd = rvtt_get_insn_data (rvtt_insn_data::sfppushc);
      gcc_assert (pushc_insnd->mod_pos < 0);
      gcall *pushc_call = gimple_build_call (pushc_insnd->decl, 0);
      gimple_set_location (pushc_call, gimple_location (icmp));
      gsi_insert_after (end, pushc_call, GSI_NEW_STMT);
    }

  gcall *setcc2_call = gimple_build_call (setcc_insnd->decl, 2);
  gimple_set_location (setcc2_call, gimple_location (icmp));
  gimple_call_set_arg (setcc2_call, 0, ssa_var);
  // 0: copy CC, 4: invert CC
  gimple_call_set_arg (setcc2_call, 1, build_int_cst (integer_type_node, cond_is_inverted ? 4 : 0));
  gsi_insert_after (end, setcc2_call, GSI_NEW_STMT);

  remove_stmt (icmp);
}

static void
finish_new_insn(gimple_stmt_iterator *gsip, bool insert_before, gimple *new_stmt, gcall *stmt)
{
  gcc_assert(new_stmt != nullptr);
  gimple_set_location (new_stmt, gimple_location (stmt));
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
emit_pushc(gimple_stmt_iterator *gsip, gcall *stmt, bool insert_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfppushc);
  gimple *new_stmt;
  if (TARGET_RVTT_GS)
    new_stmt = gimple_build_call(new_insnd->decl, 0);
  else
    new_stmt = gimple_build_call(new_insnd->decl, 1, size_int(SFPPUSHCC_MOD1_PUSH));
  finish_new_insn(gsip, insert_before, new_stmt, stmt);
}

static void
emit_popc(gimple_stmt_iterator *gsip, gcall *stmt, bool insert_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfppopc);
  gimple *new_stmt;
  if (TARGET_RVTT_GS)
    new_stmt = gimple_build_call(new_insnd->decl, 0);
  else
    new_stmt = gimple_build_call(new_insnd->decl, 1, size_int(SFPPOPCC_MOD1_POP));
  finish_new_insn(gsip, insert_before, new_stmt, stmt);
}

static void
emit_compc(gimple_stmt_iterator *gsip, gcall *stmt, bool emit_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfpcompc);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 0);
  finish_new_insn(gsip, emit_before, new_stmt, stmt);
}

static tree
emit_loadi(gimple_stmt_iterator *gsip, gcall *stmt, int val, bool emit_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfpxloadi);
  tree nullp = build_int_cst (build_pointer_type (void_type_node), 0);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 5, nullp, size_int(SFPLOADI_MOD0_SHORT), size_int(val), size_int(0), size_int(0));

  tree tmp = make_ssa_name (build_vector_type(float_type_node, 64), new_stmt);
  gimple_call_set_lhs (new_stmt, tmp);

  finish_new_insn(gsip, emit_before, new_stmt, stmt);

  return tmp;
}

static tree
emit_loadi_lv(gimple_stmt_iterator *gsip, gcall *stmt, tree lhs, tree in, int val, bool emit_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfpxloadi_lv);
  tree nullp = build_int_cst (build_pointer_type (void_type_node), 0);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 6, nullp, in, size_int(SFPLOADI_MOD0_SHORT), size_int(val), size_int(0), size_int(0));
  if (lhs == NULL_TREE)
    {
      lhs = make_ssa_name (build_vector_type(float_type_node, 64), new_stmt);
    }
  gimple_call_set_lhs (new_stmt, lhs);

  finish_new_insn(gsip, emit_before, new_stmt, stmt);

  return lhs;
}

static void
emit_setcc_v(gimple_stmt_iterator *gsip, gcall *stmt, tree in, bool emit_before)
{
  const rvtt_insn_data *new_insnd =
    rvtt_get_insn_data(rvtt_insn_data::sfpsetcc_v);
  gimple *new_stmt = gimple_build_call(new_insnd->decl, 2, in, size_int(SFPSETCC_MOD1_LREG_EQ0));
  finish_new_insn(gsip, emit_before, new_stmt, stmt);
}

static gcall *
find_top_of_cond_tree(gcall *stmt)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data(stmt);

  switch (insnd->id)
    {
    case rvtt_insn_data::sfpxfcmps:
    case rvtt_insn_data::sfpxfcmpv:
    case rvtt_insn_data::sfpxicmps:
    case rvtt_insn_data::sfpxicmpv:
      break;

    case rvtt_insn_data::sfpxbool:
      {
	// Follow only child for NOT, left-most child for AND/OR, all degenerate to same case
	gcall *child = dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXBOOL_LEFT_TREE_ARG_POS)));
	return find_top_of_cond_tree(child);
      }
      break;

    case rvtt_insn_data::sfpxcondi:
      // Should never get this deep
      gcc_assert(0);
      break;

    default:
      fprintf(stderr, "Illegal rvtt builtin found in conditional tree: %s\n", insnd->name);
      gcc_assert(0);
    }

  return stmt;
}

static void
mark_vif_stmts(gimple_stmt_iterator top,
	       gimple_stmt_iterator bot)
{
  while (top.ptr != bot.ptr &&
	 !gsi_end_p(top))
    {
      gcall* stmt;
      const rvtt_insn_data *insnd;
      if (rvtt_p(&insnd, &stmt, top))
	{
	  if (vif_stmts.find(stmt) == vif_stmts.end())
	    {
	      vif_stmts.insert(pair<gcall*, bool>(stmt, true));
	    }
	  else
	    {
	      DUMP("  already processed these stmts, bailing out\n");
	      return;
	    }
	}

      gsi_next(&top);
    }

  if (gsi_end_p(top))
    {
      // Optimizing CCs split across BBs opens up a lot of cases, bail for now
      DUMP("  didn't find xvif in same bb as xcondb, bailing out of optimization\n");
#if 0
      DUMP("  didn't find xvif in same bb as xcondb, processing BBs\n");
      basic_block bb = gsi_bb(top);
      edge_iterator ei;
      edge e;
      FOR_EACH_EDGE(e, ei, bb->succs)
	{
	  gimple_stmt_iterator next_gsi = gsi_start_bb (e->dest);
	  mark_vif_stmts(vif_stmts, next_gsi, bot);
	}
#endif
    }
}

// Expand xcondi into:
//  loadi(0)
//  pushc
//  loadi(1)
//  popc
// Returns results of loadi back to the same SSA as the xcondi for testing, up
//  to the caller to adjust the test as needed (compare against 0)
static void
expand_xcondi(gcall *stmt)
{
  gcall *child = dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXCONDI_TREE_ARG_POS)));
  gcall *top = find_top_of_cond_tree(child);

  gimple_stmt_iterator gsi = gsi_for_stmt(top);
  tree save = emit_loadi(&gsi, top, 0, true);
  emit_pushc(&gsi, top, true);
  gsi = gsi_for_stmt(child);
  tree lhs = gimple_call_lhs(stmt);
  save = emit_loadi_lv(&gsi, top, lhs, save, 1, false);
  emit_popc(&gsi, top, false);

  // Delete the stmt, but not it's DEFs!
  rvtt_prep_stmt_for_deletion(stmt);
  gsi = gsi_for_stmt(stmt);
  gsi_remove(&gsi, true);
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
  process_tree_node(pre_gsip, &left_post_gsi, &left_negated,
		    dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXBOOL_LEFT_TREE_ARG_POS))),
		    stmt, negate);

  // Emit RIGHT
  gimple_stmt_iterator right_pre_gsi;
  bool right_negated = false;
  DUMP("    right\n");
  process_tree_node(&right_pre_gsi, post_gsip, &right_negated,
		    dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXBOOL_RIGHT_TREE_ARG_POS))),
		    stmt, negate);

  if (right_negated)
    {
      DUMP("	right negated, emitting pre/post\n");
      emit_pushc(&right_pre_gsi, stmt, true);
      tree saved_enables = emit_loadi(&right_pre_gsi, stmt, 1, true);
      saved_enables = emit_loadi_lv(post_gsip, stmt, NULL_TREE, saved_enables, 0, false);
      emit_popc(post_gsip, stmt, false);
      emit_setcc_v(post_gsip, stmt, saved_enables, false);
    }

  if (negate_node)
    {
      DUMP("	node negated, emitting compc\n");
      *negated = true;
      emit_compc(post_gsip, stmt, false);
    }

  if (left_negated)
    {
      // Parent needs a fence for this node's left and side (if the parent
      // isn't the root)
      *negated = true;
    }

  DUMP("    exiting bool %d %d\n", op, negate);
}

static bool
process_xcondi(gcall *stmt, gcall *parent, bool optimizeit)
{
  // Process the child as a new tree
  gcall *child = dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXCONDI_TREE_ARG_POS)));

  bool optimized = false;
  tree cmp_lhs = gimple_call_lhs (parent);
  // These tests are redundant, but may be relevent if more cases are
  // optimized in the future
  if (optimizeit &&
      has_single_use(cmp_lhs) &&
      vif_stmts.find(stmt) != vif_stmts.end())
    {
      DUMP("  optimizing away xcondi\n");

      // Parent is an xicmps, the single vuse is an xcondb, move the
      // conditional into the xcondb and optimize away the xcondi and the
      // associated xicmps
      // Stuff the xcondi arg into the use of the icmps (and xcondi or xcondb)
      tree xcondi_op = gimple_call_arg(stmt, 0);

      gimple *xcondb_stmt;
      use_operand_p use;
      single_imm_use (cmp_lhs, &use, &xcondb_stmt);

      gimple_call_set_arg(xcondb_stmt, 0, xcondi_op);
      update_stmt(xcondb_stmt);

      remove_stmt(parent);
      remove_stmt(stmt);

      optimized = true;
    }
  else
    {
      DUMP("  expanding xcondi\n");

      // The integer conditional comparison falls outside a v_if, can't optimize
      // Instead, save the result in an int to be used later
      expand_xcondi(stmt);
    }

  process_tree(child, stmt);

  return optimized;
}

static void
process_tree_phi(gcall *stmt, gimple *child)
{
  DUMP("  process tree node phi\n");

  // Don't recurse infinitely on phi nodes
  if (phi_stmts.find(stmt) != phi_stmts.end())
    {
      return;
    }
  phi_stmts.insert(pair<gcall*, bool>(stmt, true));

  // The source of this icmps comes from multiple BBs, traverse them
  for (unsigned int i = 0; i < gimple_phi_num_args (child); i++)
    {
      gimple *origin = SSA_NAME_DEF_STMT(gimple_phi_arg_def(child, i));
      if (origin->code == GIMPLE_PHI)
	{
	  process_tree_phi(stmt, origin);
	}
      else if (origin->code == GIMPLE_CALL)
	{
	  gcall *origin_stmt = dyn_cast<gcall *>(origin);
	  const rvtt_insn_data *origin_insnd;
	  origin_insnd = rvtt_get_insn_data(origin_stmt);
	  if (origin_insnd->id == rvtt_insn_data::sfpxcondi)
	    {
	      process_tree(origin_stmt, stmt);
	    }
	}
    }
}

static void
process_tree_node(gimple_stmt_iterator *pre_gsip, gimple_stmt_iterator *post_gsip,
		  bool *negated,
		  gcall *stmt, gcall *parent,
		  bool negate)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data(stmt);
  DUMP("  process %s n:%d\n", insnd->name, negate);

  switch (insnd->id)
    {
    case rvtt_insn_data::sfpxfcmps:
    case rvtt_insn_data::sfpxfcmpv:
      {
	int mod = flip_negated_cmp(stmt, insnd, negate);
	if (cmp_issues_compc(mod)) *negated = true;
	gimple_call_set_lhs (stmt, NULL_TREE);
	*pre_gsip = *post_gsip = gsi_for_stmt(stmt);
      }
      break;

    case rvtt_insn_data::sfpxicmps:
      {
	// Note: negation happens at the use of these trees below the fall thru
	gimple *child = SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXSCMP_SRC_ARG_POS));
	if (child->code == GIMPLE_PHI)
	  {
	    process_tree_phi(stmt, child);
	  }
	else if (child->code == GIMPLE_CALL) // could be inline asm...
	  {
	    gcall *child_call = dyn_cast<gcall *>(child);
	    const rvtt_insn_data *child_insnd = rvtt_get_insn_data(child_call);
	    if (child_insnd->id == rvtt_insn_data::sfpxcondi)
	      {
		DUMP("  descending to process xcondi before xicmps\n");
		// Process child before fixing up this insn
		if (process_xcondi(child_call, stmt, true))
		  {
		    // Optimized this node away...
		    break;
		  }
	      }
	  }
      }
      // Fall thru

    case rvtt_insn_data::sfpxicmpv:
      {
	int mod = flip_negated_cmp (stmt, insnd, negate);
	if (cmp_issues_compc (mod))
	  *negated = true;

	expand_icmp (stmt, insnd, pre_gsip, post_gsip);
      }
      break;

    case rvtt_insn_data::sfpxbool:
      {
	  int op = get_int_arg(stmt, 0);
	  if (op == SFPXBOOL_MOD1_NOT)
	    {
	      process_tree_node(pre_gsip, post_gsip, negated,
				dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, 1))), stmt, !negate);
	    }
	  else
	    {
	      process_bool_tree(pre_gsip, post_gsip, negated, stmt, op, negate);
	    }
	  remove_stmt(stmt);
      }
      break;

    case rvtt_insn_data::sfpxcondi:
      process_xcondi(stmt, parent, false);
      break;

    default:
      fprintf(stderr, "Illegal rvtt builtin found in conditional tree: %s\n", insnd->name);
      gcc_assert(0);
    }
}

static void
process_tree(gcall *stmt, gcall *parent)
{
  bool negated = false;
  gimple_stmt_iterator pre_gsi, post_gsi;

  process_tree_node(&pre_gsi, &post_gsi, &negated, stmt, parent, false);
}

// Expand boolean trees
//
// The hardware does not support OR and generates some comparisons (LTE, GE)
// by ANDing others together and issuing a compc.  This requires refactoring
// boolean expressions using De Moragan's laws.	 The root of a tree is anchored
// by an sfpxcondb.  All dependent operations are chained to this by their
// return values.  This pass traverses the tree, more or less deletes it and
// replaces it with one that works w/ the HW.
static void
transform (function *fun)
{
  DUMP("Expand pass on: %s\n", function_name(fun));

  phi_stmts.reserve(20);
  vif_stmts.reserve(20);
  basic_block bb;
  gimple_stmt_iterator gsi;

  // Must process xcondis in all BBs before xcondbs because vif stmts can fall
  // in a BB other than the one containing the associated xcondb
  FOR_EACH_BB_FN (bb, fun)
    {
      DUMP("  bb process vif loop\n");
      gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gimple_stmt_iterator next_gsi = gsi;
	  gsi_next(&next_gsi);

	  gcall *stmt;
	  const rvtt_insn_data *insnd;
	  if (rvtt_p(&insnd, &stmt, gsi) &&
	      insnd->id == rvtt_insn_data::sfpxcondb)
	    {
	      DUMP("  process xcondb\n");
	      // This will be the sfpxvif stmt
	      gcall *child = dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXCONDB_TREE_ARG_POS)));
	      gcall* top = dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXCONDB_START_ARG_POS)));
	      mark_vif_stmts(gsi_for_stmt(top), gsi);

	      process_tree(child, stmt);

	      remove_stmt(stmt);
	      remove_stmt(top);
	      vif_stmts.clear();
	      phi_stmts.clear();
	    }

	  gsi = next_gsi;
	}
    }

  // Now process any xcondis that aren't associated w/ a xcondbs
  FOR_EACH_BB_FN (bb, fun)
    {
      DUMP("  bb process outside vif loop\n");
      gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gimple_stmt_iterator next_gsi = gsi;
	  gsi_next(&next_gsi);

	  gcall *stmt;
	  const rvtt_insn_data *insnd;
	  if (rvtt_p(&insnd, &stmt, gsi))
	    {
	      if (insnd->id == rvtt_insn_data::sfpxcondi)
		{
		  DUMP("  process xcondi tree\n");
		  gcall *child = dyn_cast<gcall *>(SSA_NAME_DEF_STMT(gimple_call_arg(stmt, SFPXCONDI_TREE_ARG_POS)));
		  expand_xcondi(stmt);
		  process_tree(child, stmt);
		  phi_stmts.clear();
		}
	    }

	  gsi = next_gsi;
	}
    }

  update_ssa (TODO_update_ssa);
}

namespace {

const pass_data pass_data_rvtt_expand =
{
  GIMPLE_PASS, /* type */
  "rvtt_expand", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_expand : public gimple_opt_pass
{
public:
  pass_rvtt_expand (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_expand, ctxt)
  {}

  virtual unsigned int execute (function *);
}; // class pass_rvtt_expand

} // anon namespace

/* Entry point to rvtt_expand pass.	*/
unsigned int
pass_rvtt_expand::execute (function *fun)
{
  if (TARGET_RVTT)
    transform (fun);

  return 0;
}

gimple_opt_pass *
make_pass_rvtt_expand (gcc::context *ctxt)
{
  return new pass_rvtt_expand (ctxt);
}
