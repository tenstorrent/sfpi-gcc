/* Pass to select Tensix SFPU LUT instructions from range-dispatch trees.
   Copyright (C) 2026 Tenstorrent Inc.

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

/* This pass recognizes the DATAFLOW SHAPE of a magnitude range-dispatch
   tree -- a predicated select tree over range-partitioned affine leaves
   feeding multiply-add, all keyed by one float magnitude |x| -- and
   re-selects it as a single hardware LUT instruction when (and only
   when) a target capability table proves the partition is encodable:

     mag = sfpabs (x, float)
     r   = A2 * mag + B2			  default leaf (last range)
     pushc; setcc (mag < boundary0)
       r = A0 * mag + B0			  range-0 leaf
     compc; pushc; setcc (mag < boundary1)
       r = A1 * mag + B1			  range-1 leaf
     popc; popc
	==>
     r = sfplutfp32_6r (A0, A1, A2, B0, B1, B2, x, mod0)

   The matcher is purely structural: coefficients are arbitrary SSA
   values (renaming them or changing their values must not change the
   decision), while everything architectural -- the emission builtin,
   its mode word, the partition arity, and the exact boundary
   encodings the hardware buckets |x| with -- comes from the target
   capability table in rvtt-lut-tables.cc.  Any deviation from the
   proven shape refuses with a named reason; a refusal never edits the
   program.

   Equivalence notes (first increment, FP32 3-entry, SGN_UPDATE):
   - the hardware buckets on strict magnitude-bit compares of |x|;
     for the proven non-negative magnitude these order identically to
     the tree's float less-than compares, including for NaN (bucket
     num_ranges-1 on both paths) and infinities;
   - the hardware evaluates fma (A_i, |x|, B_i) with a single
     rounding; the tree's separate mul+add pair is already fused into
     the same single-rounding SFPMAD by the default-on rvtt combine
     pass, so selection introduces no new rounding difference relative
     to the default pipeline;
   - lanes disabled by an enclosing condition context are untouched by
     both forms (the LUT executes under the same enclosing CC that
     gated the tree's leaf writes).  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "rtl.h"
#include "tree.h"
#include "gimple.h"
#include "tree-pass.h"
#include "ssa.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "tree-into-ssa.h"
#include "tree-cfg.h"
#include "rvtt.h"
#include "rvtt-lut-tables.h"

namespace {

/* Everything discovered about one candidate dispatch-tree group.  */
struct lut_group
{
  /* Structural statements, in program order.  */
  gimple *pushc[2];
  gimple *xvif[2];
  gimple *fcmp[2];
  gimple *condb[2];
  gimple *assign[2];
  gimple *compc;
  gimple *popc[2];

  /* Leaf computations.  Index 0/1 = predicated leaves, 2 = default
     leaf.  A leaf is either mul+add or a single mad (mul null).  */
  gimple *leaf_mul[3];
  gimple *leaf_add[3];

  /* The magnitude, its defining abs, and the abs input.  */
  tree mag;
  gimple *abs_stmt;
  tree x;

  /* Coefficients, indexed by range bucket.  */
  tree a_coeff[3];
  tree b_coeff[3];

  /* Boundary encodings found on the compares, in tree order.  */
  uint32_t boundary_bits[2];

  /* Result SSA name (lhs of the final live-value assign).  */
  tree result;

  lut_group () { memset (this, 0, sizeof (*this)); }
};

static unsigned n_formed;
static unsigned n_refused;

/* Named refusal.  Returns false so matchers can tail-call it.  */

static bool
refuse (const char *reason, gimple *stmt)
{
  n_refused++;
  if (dump_file)
    {
      fprintf (dump_file, "lut-select: refused (%s): ", reason);
      if (stmt)
	print_gimple_stmt (dump_file, stmt, 0);
      else
	fprintf (dump_file, "\n");
    }
  return false;
}

static long
int_arg (gcall *stmt, unsigned arg)
{
  tree t = gimple_call_arg (stmt, arg);
  if (t && TREE_CODE (t) == INTEGER_CST)
    return TREE_INT_CST_LOW (t);
  return -1;
}

/* Return the rvtt insn data if STMT is a call to rvtt insn ID.  */

static gcall *
is_rvtt_call (gimple *stmt, rvtt_insn_data::insn_id id)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
  if (insnd && insnd->id == id)
    return as_a <gcall *> (stmt);
  return nullptr;
}

/* Match VAL as an affine function of MAG: mul+add or a single mad,
   with all-default instruction modes.  On success fill A/B and the
   defining statements (S_MUL may stay null for a mad).  */

static bool
match_affine_leaf (tree val, tree mag, tree *a, tree *b,
		   gimple **s_mul, gimple **s_add)
{
  if (TREE_CODE (val) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (val);

  if (gcall *mad = is_rvtt_call (def, rvtt_insn_data::sfpmad))
    {
      if (int_arg (mad, 3) != 0)
	return false;
      tree m0 = gimple_call_arg (mad, 0);
      tree m1 = gimple_call_arg (mad, 1);
      if (m0 == mag && m1 != mag)
	*a = m1;
      else if (m1 == mag && m0 != mag)
	*a = m0;
      else
	return false;
      *b = gimple_call_arg (mad, 2);
      *s_mul = nullptr;
      *s_add = def;
      return true;
    }

  gcall *add = is_rvtt_call (def, rvtt_insn_data::sfpadd);
  if (!add || int_arg (add, 2) != 0)
    return false;

  /* One add operand must be a single-use mul by MAG; the other is B.  */
  for (int mul_ix = 0; mul_ix < 2; mul_ix++)
    {
      tree mval = gimple_call_arg (add, mul_ix);
      tree oval = gimple_call_arg (add, 1 - mul_ix);
      if (TREE_CODE (mval) != SSA_NAME)
	continue;
      gcall *mul = is_rvtt_call (SSA_NAME_DEF_STMT (mval),
				 rvtt_insn_data::sfpmul);
      if (!mul || int_arg (mul, 2) != 0 || !has_single_use (mval))
	continue;
      tree m0 = gimple_call_arg (mul, 0);
      tree m1 = gimple_call_arg (mul, 1);
      tree coeff;
      if (m0 == mag && m1 != mag)
	coeff = m1;
      else if (m1 == mag && m0 != mag)
	coeff = m0;
      else
	continue;
      *a = coeff;
      *b = oval;
      *s_mul = mul;
      *s_add = def;
      return true;
    }
  return false;
}

/* Check that the fcmp statement compares MAG against an integer
   float-bit constant with a float strict less-than, and return the
   constant through *BITS.  */

static bool
match_lt_boundary (gcall *fcmp, tree mag, uint32_t *bits)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (fcmp);
  if (gimple_call_arg (fcmp, 1) != mag)
    return false;
  long mod = int_arg (fcmp, insnd->mod_arg ());
  if (mod != ((long)(SFPXCMP_MOD1_TYPE_FLOAT << SFPXCMP_MOD1_TYPE_SHIFT)
	      | SFPXCMP_MOD1_CC_LT))
    return false;
  long k = int_arg (fcmp, 2);
  if (k < 0)
    return false;
  *bits = (uint32_t) k;
  return true;
}

/* Collect the contiguous region starting at the sfppushc at *GSI up
   to the matching CC-stack balance point, classifying its statements
   into G.  Returns true when the full structural skeleton matched.
   REPORT enables named refusals (set once the region has identified
   itself as a magnitude dispatch candidate).  */

static bool
match_group (gimple_stmt_iterator gsi, lut_group *g, bool *candidate)
{
  /* Structural cursor: which skeleton statement we expect next.  */
  enum {
    WANT_XVIF0, WANT_FCMP0, WANT_CONDB0, WANT_ASSIGN0,
    WANT_COMPC, WANT_PUSHC1, WANT_XVIF1, WANT_FCMP1, WANT_CONDB1,
    WANT_ASSIGN1, WANT_POPC0, WANT_POPC1, DONE
  } want = WANT_XVIF0;

  *candidate = false;

  gimple *stmt = gsi_stmt (gsi);
  gcall *pushc0 = is_rvtt_call (stmt, rvtt_insn_data::sfppushc);
  if (!pushc0 || int_arg (pushc0, 0) != 0)
    return false;
  g->pushc[0] = pushc0;

  /* Statements not yet proven to be leaf computation.  Leaf value
     recognition happens when the live-value assign is reached; any
     statement left unclaimed at the end refuses the group.  */
  auto_vec<gimple *, 16> pending;

  unsigned depth = 1;
  gsi_next (&gsi);
  for (unsigned steps = 0; !gsi_end_p (gsi) && steps < 64; gsi_next (&gsi))
    {
      stmt = gsi_stmt (gsi);
      /* Debug binds are not semantic statements and must not count
	 against the scan budget (-g must not change decisions).  */
      if (is_gimple_debug (stmt))
	continue;
      steps++;

      const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
      if (!insnd)
	return *candidate
	  ? refuse ("lut-region-foreign-stmt", stmt) : false;
      gcall *call = as_a <gcall *> (stmt);

      switch (insnd->id)
	{
	case rvtt_insn_data::sfpxvif:
	  if (want == WANT_XVIF0)
	    g->xvif[0] = stmt, want = WANT_FCMP0;
	  else if (want == WANT_XVIF1)
	    g->xvif[1] = stmt, want = WANT_FCMP1;
	  else
	    return *candidate
	      ? refuse ("lut-structure-mismatch", stmt) : false;
	  continue;

	case rvtt_insn_data::sfpxfcmps:
	  if (want == WANT_FCMP0)
	    {
	      /* Candidate identification: a float compare whose vector
		 operand is a float abs.  From here on refusals are
		 reported.  */
	      tree v = gimple_call_arg (call, 1);
	      if (TREE_CODE (v) == SSA_NAME)
		if (gcall *abs = is_rvtt_call (SSA_NAME_DEF_STMT (v),
					       rvtt_insn_data::sfpabs))
		  if (int_arg (abs, 1) == 1)
		    {
		      *candidate = true;
		      g->mag = v;
		      g->abs_stmt = abs;
		      g->x = gimple_call_arg (abs, 0);
		    }
	      if (!*candidate)
		return false;
	      if (!match_lt_boundary (call, g->mag, &g->boundary_bits[0]))
		return refuse ("lut-compare-kind-unsupported", stmt);
	      g->fcmp[0] = stmt;
	      want = WANT_CONDB0;
	    }
	  else if (want == WANT_FCMP1)
	    {
	      if (!match_lt_boundary (call, g->mag, &g->boundary_bits[1]))
		return refuse ("lut-compare-kind-unsupported", stmt);
	      g->fcmp[1] = stmt;
	      want = WANT_CONDB1;
	    }
	  else
	    return *candidate
	      ? refuse ("lut-structure-mismatch", stmt) : false;
	  continue;

	case rvtt_insn_data::sfpxcondb:
	  {
	    int ix = want == WANT_CONDB0 ? 0 : want == WANT_CONDB1 ? 1 : -1;
	    if (ix < 0)
	      return *candidate
		? refuse ("lut-structure-mismatch", stmt) : false;
	    /* The condition tree must be exactly the compare, anchored
	       at this region's vif token.  */
	    tree c = gimple_call_arg (call, 0);
	    tree t = gimple_call_arg (call, 1);
	    if (TREE_CODE (c) != SSA_NAME || TREE_CODE (t) != SSA_NAME
		|| SSA_NAME_DEF_STMT (c) != g->fcmp[ix]
		|| SSA_NAME_DEF_STMT (t) != g->xvif[ix]
		|| !has_single_use (c) || !has_single_use (t))
	      return refuse ("lut-structure-mismatch", stmt);
	    g->condb[ix] = stmt;
	    want = ix == 0 ? WANT_ASSIGN0 : WANT_ASSIGN1;
	  }
	  continue;

	case rvtt_insn_data::sfpassign_lv:
	  {
	    int ix = want == WANT_ASSIGN0 ? 0 : want == WANT_ASSIGN1 ? 1 : -1;
	    if (ix < 0)
	      return *candidate
		? refuse ("lut-structure-mismatch", stmt) : false;
	    g->assign[ix] = stmt;
	    want = ix == 0 ? WANT_COMPC : WANT_POPC0;
	  }
	  continue;

	case rvtt_insn_data::sfpcompc:
	  if (want != WANT_COMPC)
	    /* A further else-branch after the second range means the
	       tree partitions more ranges than the capability table's
	       arity.  */
	    return *candidate
	      ? refuse (want == WANT_POPC0
			? "lut-partition-arity-unsupported"
			: "lut-structure-mismatch", stmt) : false;
	  g->compc = stmt;
	  want = WANT_PUSHC1;
	  continue;

	case rvtt_insn_data::sfppushc:
	  if (want != WANT_PUSHC1 || int_arg (call, 0) != 0)
	    return *candidate
	      ? refuse ("lut-partition-arity-unsupported", stmt) : false;
	  depth++;
	  g->pushc[1] = stmt;
	  want = WANT_XVIF1;
	  continue;

	case rvtt_insn_data::sfppopc:
	  {
	    int ix = want == WANT_POPC0 ? 0 : want == WANT_POPC1 ? 1 : -1;
	    if (ix < 0 || int_arg (call, 0) != 0)
	      /* Closing right after the first range's assign means a
	         two-range partition: below the table's arity.  */
	      return *candidate
		? refuse (want == WANT_COMPC
			  ? "lut-partition-arity-unsupported"
			  : "lut-structure-mismatch", stmt) : false;
	    g->popc[ix] = stmt;
	    depth--;
	    want = ix == 0 ? WANT_POPC1 : DONE;
	    if (want == DONE)
	      {
		gcc_assert (depth == 0);
		/* Region closed: prove the leaves.  */
		goto region_closed;
	      }
	  }
	  continue;

	default:
	  /* Possible leaf computation or coefficient definition;
	     resolved once the assigns are known.  */
	  if (insnd->has_side_effects (call))
	    return *candidate
	      ? refuse ("lut-region-foreign-stmt", stmt) : false;
	  pending.safe_push (stmt);
	  continue;
	}
    }
  return *candidate ? refuse ("lut-region-open-cfg", g->pushc[0]) : false;

 region_closed:
  /* Recognize the two predicated leaves through their live-value
     assigns: assign[ix] = sfpassign_lv (old, new).  */
  tree old0 = gimple_call_arg (as_a <gcall *> (g->assign[0]), 0);
  tree new0 = gimple_call_arg (as_a <gcall *> (g->assign[0]), 1);
  tree old1 = gimple_call_arg (as_a <gcall *> (g->assign[1]), 0);
  tree new1 = gimple_call_arg (as_a <gcall *> (g->assign[1]), 1);
  tree r1 = gimple_call_lhs (g->assign[0]);

  if (old1 != r1 || !has_single_use (r1))
    return refuse ("lut-structure-mismatch", g->assign[1]);
  g->result = gimple_call_lhs (g->assign[1]);
  if (!g->result)
    return refuse ("lut-structure-mismatch", g->assign[1]);

  if (!match_affine_leaf (new0, g->mag, &g->a_coeff[0], &g->b_coeff[0],
			  &g->leaf_mul[0], &g->leaf_add[0]))
    return refuse ("lut-leaf-not-affine", g->assign[0]);
  if (!match_affine_leaf (new1, g->mag, &g->a_coeff[1], &g->b_coeff[1],
			  &g->leaf_mul[1], &g->leaf_add[1]))
    return refuse ("lut-leaf-not-affine", g->assign[1]);
  /* The default (last-range) leaf reaches the tree as the first
     assign's incoming live value.  */
  if (!match_affine_leaf (old0, g->mag, &g->a_coeff[2], &g->b_coeff[2],
			  &g->leaf_mul[2], &g->leaf_add[2]))
    return refuse ("lut-default-leaf-unproven", g->assign[0]);

  /* Leaf values must be consumed only by their assigns, and the
     default leaf only by the tree, so deleting the tree orphans
     nothing.  */
  if (!has_single_use (new0) || !has_single_use (new1)
      || !has_single_use (old0))
    return refuse ("lut-leaf-value-escapes", g->assign[0]);

  /* The predicated leaves' computations must live inside the region
     (between the region's pushc and popc) so their statements are
     accounted for; the default leaf must be defined before the region
     in the same block.  Verify by claiming PENDING.  */
  auto claimed_p = [&] (gimple *stmt) -> bool
    {
      for (int leaf = 0; leaf < 2; leaf++)
	if (stmt == g->leaf_mul[leaf] || stmt == g->leaf_add[leaf])
	  return true;
      /* Coefficient definitions may sit inside the region when every
	 use is a claimed leaf statement or another coefficient (all
	 pure): they simply outlive the tree as LUT operands.  */
      tree lhs = gimple_call_lhs (stmt);
      if (!lhs)
	return false;
      for (int i = 0; i < 3; i++)
	if (lhs == g->a_coeff[i] || lhs == g->b_coeff[i])
	  return true;
      return false;
    };
  for (gimple *stmt : pending)
    if (!claimed_p (stmt))
      return refuse ("lut-region-foreign-stmt", stmt);

  for (int leaf = 2; leaf >= 0; leaf--)
    {
      gimple *stmts[2] = { g->leaf_mul[leaf], g->leaf_add[leaf] };
      for (gimple *stmt : stmts)
	if (stmt && gimple_bb (stmt) != gimple_bb (g->pushc[0]))
	  return refuse (leaf == 2 ? "lut-default-leaf-unproven"
			 : "lut-leaf-value-escapes", stmt);
    }

  /* Shape proven.  Now the capability check: exactly this partition
     arity, with these boundary encodings, in ascending range order,
     with no sign restore, must exist on this target.  */
  const rvtt_lut_mode_desc *mode = rvtt_lut_lookup (3, false);
  if (!mode)
    return refuse ("lut-no-target-capability", g->pushc[0]);
  for (unsigned i = 0; i < mode->num_ranges - 1; i++)
    if (g->boundary_bits[i] != mode->boundary_bits[i])
      return refuse ("lut-boundary-mismatch", g->fcmp[i]);

  /* Materialize.  */
  const rvtt_insn_data *lut_insnd = rvtt_get_insn_data (mode->insn);
  gcc_assert (lut_insnd->decl);

  gcall *lut = gimple_build_call
    (lut_insnd->decl, 8,
     g->a_coeff[0], g->a_coeff[1], g->a_coeff[2],
     g->b_coeff[0], g->b_coeff[1], g->b_coeff[2],
     g->x, build_int_cst (unsigned_type_node, mode->mod0));
  gimple_call_set_lhs (lut, g->result);
  gimple_set_location (lut, gimple_location (g->assign[1]));

  gimple_stmt_iterator rsi = gsi_for_stmt (g->assign[1]);
  gsi_replace (&rsi, lut, false);

  if (dump_file)
    {
      fprintf (dump_file,
	       "lut-select: formed %s (mod0 %#x) from 3-range magnitude"
	       " dispatch tree, boundaries %#x,%#x: ",
	       mode->name, mode->mod0,
	       mode->boundary_bits[0], mode->boundary_bits[1]);
      print_gimple_stmt (dump_file, lut, 0);
    }

  /* Delete the tree: users before definers so nothing is orphaned.
     Coefficient definitions stay (they now feed the LUT).  */
  auto remove = [] (gimple *stmt)
    {
      if (!stmt)
	return;
      rvtt_prep_stmt_for_deletion (stmt);
      unlink_stmt_vdef (stmt);
      gimple_stmt_iterator gsi = gsi_for_stmt (stmt);
      gsi_remove (&gsi, true);
      release_defs (stmt);
    };

  remove (g->assign[0]);
  remove (g->condb[0]);
  remove (g->condb[1]);
  remove (g->compc);
  remove (g->popc[0]);
  remove (g->popc[1]);
  remove (g->pushc[0]);
  remove (g->pushc[1]);
  remove (g->xvif[0]);
  remove (g->xvif[1]);
  remove (g->fcmp[0]);
  remove (g->fcmp[1]);
  for (int leaf = 0; leaf < 3; leaf++)
    {
      remove (g->leaf_add[leaf]);
      remove (g->leaf_mul[leaf]);
    }
  /* The magnitude may now be dead (the LUT recomputes |x|
     internally); the abs input feeds the LUT instead.  A null lhs
     means the recursive single-use cleanup above already stripped
     it.  */
  tree abs_lhs = gimple_call_lhs (g->abs_stmt);
  if (!abs_lhs || has_zero_uses (abs_lhs))
    remove (g->abs_stmt);

  n_formed++;
  return true;
}

static bool
transform (function *fun)
{
  bool changed = false;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    {
      gimple_stmt_iterator gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gimple_stmt_iterator next = gsi;
	  gsi_next (&next);
	  if (is_rvtt_call (gsi_stmt (gsi), rvtt_insn_data::sfppushc))
	    {
	      lut_group g;
	      bool candidate;
	      if (match_group (gsi, &g, &candidate))
		{
		  changed = true;
		  /* The group's statements are gone; restart after the
		     formed LUT.  */
		  next = gsi_for_stmt (SSA_NAME_DEF_STMT (g.result));
		  gsi_next (&next);
		}
	    }
	  gsi = next;
	}
    }
  return changed;
}

const pass_data pass_data_rvtt_lut_select =
{
  GIMPLE_PASS,
  "rvtt_lut_select",
  OPTGROUP_NONE,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_rvtt_lut_select : public gimple_opt_pass
{
public:
  pass_rvtt_lut_select (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_lut_select, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_lut_select > 0;
  }

  unsigned execute (function *fun) final override
  {
    n_formed = 0;
    n_refused = 0;
    if (TARGET_XTT_TENSIX_QSR)
      {
	if (dump_file)
	  fprintf (dump_file, "lut-select: refused (lut-no-target-capability):"
		   " QSR has no validated LUT capability\n");
	return 0;
      }
    bool changed = transform (fun);
    if (dump_file)
      fprintf (dump_file, "lut-select: groups=%u refusals=%u\n",
	       n_formed, n_refused);
    return changed ? TODO_update_ssa_only_virtuals | TODO_verify_all : 0;
  }
};

} // anonymous namespace

gimple_opt_pass *
make_pass_rvtt_lut_select (gcc::context *ctxt)
{
  return new pass_rvtt_lut_select (ctxt);
}
