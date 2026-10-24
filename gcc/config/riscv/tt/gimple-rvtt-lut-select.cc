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

/* This pass recognizes a magnitude range-dispatch tree -- a predicated
   select tree over range-partitioned affine leaves, all keyed by one
   float magnitude |x| -- and replaces it with a single SFPLUTFP32 when a
   target capability table shows the partition is encodable:

     mag = sfpabs (x, float)
     r   = A2 * mag + B2			  default leaf (last range)
     pushc; setcc (mag < boundary0)
       r = A0 * mag + B0			  range-0 leaf
     compc; pushc; setcc (mag < boundary1)
       r = A1 * mag + B1			  range-1 leaf
     popc; popc
   	==>
     r = sfplutfp32_6r (A0, A1, A2, B0, B1, B2, x, mod0)

   The matcher is structural: coefficients are arbitrary SSA values.  The
   emission builtin, mode word, partition arity and boundary encodings all
   come from the capability table in rvtt-lut-tables.cc.  Any deviation
   refuses with a named reason and leaves the program unchanged.

   The replacement is exact because (1) the hardware buckets |x| with
   strict magnitude compares that order identically to the tree's float
   compares, including NaN and infinities, on Blackhole; on Wormhole a
   negative NaN diverges, so formation there requires -ffinite-math-only;
   (2) the LUT evaluates fma (A_i, |x|, B_i) with one rounding, as does the
   SFPMAD that the rvtt combine pass forms from each leaf; (3) lanes
   disabled by an enclosing CC context are untouched by both forms.

   A trailing sign copy of the LUT input onto the result is folded into the
   mode word when the table offers a sign-restore mode.  After formation
   the coefficient materializations are ordinary loop invariants, so they
   are moved to the loop preheader, all or none, within the eight-LREG
   budget.  -mtt-tensix-optimize-lut-select-leaf-ext adds multiply-only and
   constant leaves and trees with fewer ranges than the mode (leaves are
   duplicated across slots), each admitted only where rvtt-lut-tables.cc
   records a bit-exact certification.  -mtt-tensix-optimize-lut-select-fp16
   adds the six-entry FP16 table modes, whose coefficients must be
   compile-time constants that re-encode exactly in LUT16 format.  */

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
#include "cfghooks.h"
#include "cfgloop.h"
#include "dominance.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-pressure.h"
#include "rvtt-refuse.h"
#include "rvtt-lut-tables.h"
#include "rvtt-macro-ownership.h"
#include "rvtt-cc-region.h"

namespace {

/* Architectural ceiling over every table row's arity.  */
#define RVTT_LUT_MAX_RANGES 6

/* Leaf classes the matcher recognizes.  The non-affine classes are
   matched only under the leaf extension.  */
enum lut_leaf_kind
{
  LUT_LEAF_AFFINE,
  LUT_LEAF_MUL0,	/* mul-only: slot B is a synthesized +0.0.  */
  LUT_LEAF_CONST,	/* provable constant: slot A is a synthesized
			   +0.0, slot B is the constant's own SSA.  */
};

/* Everything discovered about one candidate dispatch-tree group.  */
struct lut_group
{
  /* Structural statements, in program order.  Index = predicated
     region.  */
  gimple *pushc[RVTT_LUT_MAX_RANGES - 1];
  gimple *xvif[RVTT_LUT_MAX_RANGES - 1];
  gimple *fcmp[RVTT_LUT_MAX_RANGES - 1];
  gimple *condb[RVTT_LUT_MAX_RANGES - 1];
  gimple *assign[RVTT_LUT_MAX_RANGES - 1];
  gimple *end;
  gimple *compc[RVTT_LUT_MAX_RANGES - 2];
  gimple *popc[RVTT_LUT_MAX_RANGES - 1];

  /* Number of predicated regions matched (1 to RVTT_LUT_MAX_RANGES-1;
     below full arity only under the leaf extension / fp16 gates).  */
  unsigned num_pred;

  /* Leaf computations.  Index 0..num_pred-1 = predicated leaves,
     num_pred = default leaf.  An affine leaf is either mul+add or a
     single mad (mul null); mul-only and constant leaves per KIND.  */
  gimple *leaf_mul[RVTT_LUT_MAX_RANGES];
  gimple *leaf_add[RVTT_LUT_MAX_RANGES];
  lut_leaf_kind leaf_kind[RVTT_LUT_MAX_RANGES];
  uint32_t leaf_const_bits[RVTT_LUT_MAX_RANGES];

  /* The magnitude, its defining abs, and the abs input.  */
  tree mag;
  gimple *abs_stmt;
  tree x;

  /* Coefficients, indexed like the leaves.  NULL_TREE = synthesize
     the certified +0.0 for that slot position.  */
  tree a_coeff[RVTT_LUT_MAX_RANGES];
  tree b_coeff[RVTT_LUT_MAX_RANGES];

  /* Boundary encodings found on the compares, in tree order.  */
  uint32_t boundary_bits[RVTT_LUT_MAX_RANGES - 1];

  /* The statement materializing each region's boundary.  sfpxcmp takes the
     boundary as a vector operand, so it is a statement inside the region
     rather than an immediate on the compare, and the region scan has to
     account for it.  */
  gimple *boundary_def[RVTT_LUT_MAX_RANGES - 1];

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
  rvtt_refuse_by_name_at (reason, stmt, dump_file,
			  "lut-select: refused (%s): ", reason);
  if (dump_file)
    {
      if (stmt)
	print_gimple_stmt (dump_file, stmt, 0);
      else
	fprintf (dump_file, "\n");
    }
  return false;
}

/* If DEF is a constant definition whose 32-bit lane value is known,
   store it in *BITS and return true.  The 32-bit sfpxloadi forms carry
   the pattern verbatim, SFPLOADI FLOATB is imm16 << 16, SFPLOADI FLOATA
   is the fp16 exponent rebias (s << 16) | ((em + (112 << 10)) << 13)
   per SFPLOADI.md, a USHORT/UPPER pair combines its halves, and reads
   of the read-only constant registers give LReg[9] = 0.0 and
   LReg[10] = 1.0.  */

static bool
const_leaf_value_p (gimple *def, uint32_t *bits)
{
  gcall *call = dyn_cast <gcall *> (def);
  if (!call)
    return false;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd)
    return false;

  if (insnd->id == rvtt_insn_data::sfpreadlreg)
    {
      long creg = rvtt_call_int_arg (call, 0);
      if (creg == 9)
	*bits = 0x00000000u;
      else if (creg == 10)
	*bits = 0x3f800000u;
      else
	return false;
      return true;
    }

  /* The two-instruction form emit_loadimm issues when both halves of a
     32-bit constant are significant: SFPLOADI USHORT of the low half, then
     SFPLOADI_LV UPPER of the high half.  Immediate lowering runs before
     this pass, so such constants arrive as this pair, which reconstructs
     exactly.  */
  if (insnd->id == rvtt_insn_data::sfploadi_lv)
    {
      tree hi = gimple_call_arg (call, insnd->imm_arg ());
      tree hi_mod = gimple_call_arg (call, insnd->mod_arg ());
      tree link = gimple_call_arg (call, 1);
      if (TREE_CODE (hi) != INTEGER_CST || TREE_CODE (hi_mod) != INTEGER_CST
	  || TREE_INT_CST_LOW (hi_mod) != SFPLOADI_MOD0_UPPER
	  || TREE_CODE (link) != SSA_NAME)
	return false;
      gcall *root = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (link));
      const rvtt_insn_data *rootd = root ? rvtt_get_insn_data (root) : nullptr;
      if (!rootd || rootd->id != rvtt_insn_data::sfploadi)
	return false;
      tree lo = gimple_call_arg (root, rootd->imm_arg ());
      tree lo_mod = gimple_call_arg (root, rootd->mod_arg ());
      if (TREE_CODE (lo) != INTEGER_CST || TREE_CODE (lo_mod) != INTEGER_CST
	  || TREE_INT_CST_LOW (lo_mod) != SFPLOADI_MOD0_USHORT)
	return false;
      *bits = ((((uint32_t) TREE_INT_CST_LOW (hi)) & 0xffff) << 16)
	      | (((uint32_t) TREE_INT_CST_LOW (lo)) & 0xffff);
      return true;
    }

  if (insnd->id != rvtt_insn_data::sfpxloadi
      && insnd->id != rvtt_insn_data::sfploadi)
    return false;
  for (unsigned ix = 1; ix != gimple_call_num_args (call); ++ix)
    if (TREE_CODE (gimple_call_arg (call, ix)) != INTEGER_CST)
      return false;
  tree imm = gimple_call_arg (call, 1);
  tree mod = gimple_call_arg (call, gimple_call_num_args (call) - 1);
  if (insnd->id == rvtt_insn_data::sfpxloadi)
    {
      HOST_WIDE_INT m = tree_to_shwi (mod);
      if (m != 31 && m != 32 && m != -32)
	return false;
      *bits = (uint32_t) TREE_INT_CST_LOW (imm);
      return true;
    }
  /* Shortened SFPLOADI: FLOATB and FLOATA.  */
  HOST_WIDE_INT m0 = tree_to_shwi (mod);
  uint32_t imm16 = (uint32_t) TREE_INT_CST_LOW (imm) & 0xffff;
  if (m0 == SFPLOADI_MOD0_FLOATB)
    {
      *bits = imm16 << 16;
      return true;
    }
  if (m0 == SFPLOADI_MOD0_FLOATA)
    {
      uint32_t s = imm16 & 0x8000;
      uint32_t em = imm16 & 0x7fff;
      *bits = (s << 16) | ((em + (112u << 10)) << 13);
      return true;
    }
  return false;
}

/* Match VAL as leaf IX of G: an affine function of MAG (mul+add or a
   single mad, all-default instruction modes), or -- under the leaf
   extension -- a mul-only leaf or a provable constant leaf.  On
   success fill the leaf's coefficients, kind, and defining
   statements.  */

static bool
match_leaf (lut_group *g, unsigned ix, tree val, tree mag)
{
  tree *a = &g->a_coeff[ix];
  tree *b = &g->b_coeff[ix];
  gimple **s_mul = &g->leaf_mul[ix];
  gimple **s_add = &g->leaf_add[ix];
  g->leaf_kind[ix] = LUT_LEAF_AFFINE;

  if (TREE_CODE (val) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (val);

  if (gcall *mad = rvtt_call_with_id (def, rvtt_insn_data::sfpmad))
    {
      if (rvtt_call_int_arg (mad, 3) != 0)
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

  gcall *add = rvtt_call_with_id (def, rvtt_insn_data::sfpadd);
  if (add && rvtt_call_int_arg (add, 2) == 0)
    /* One add operand must be a single-use mul by MAG; the other is
       B.  */
    for (int mul_ix = 0; mul_ix < 2; mul_ix++)
      {
	tree mval = gimple_call_arg (add, mul_ix);
	tree oval = gimple_call_arg (add, 1 - mul_ix);
	if (TREE_CODE (mval) != SSA_NAME)
	  continue;
	gcall *mul = rvtt_call_with_id (SSA_NAME_DEF_STMT (mval),
				   rvtt_insn_data::sfpmul);
	if (!mul || rvtt_call_int_arg (mul, 2) != 0 || !has_single_use (mval))
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

  if (!riscv_tt_opt_lut_select_leaf_ext)
    return false;

  /* Mul-only leaf: the slot's B coefficient will be a synthesized
     +0.0.  */
  if (gcall *mul = rvtt_call_with_id (def, rvtt_insn_data::sfpmul))
    if (rvtt_call_int_arg (mul, 2) == 0)
      {
	tree m0 = gimple_call_arg (mul, 0);
	tree m1 = gimple_call_arg (mul, 1);
	tree coeff = NULL_TREE;
	if (m0 == mag && m1 != mag)
	  coeff = m1;
	else if (m1 == mag && m0 != mag)
	  coeff = m0;
	if (coeff)
	  {
	    *a = coeff;
	    *b = NULL_TREE;
	    *s_mul = def;
	    *s_add = nullptr;
	    g->leaf_kind[ix] = LUT_LEAF_MUL0;
	    return true;
	  }
      }

  /* Provable constant leaf: the slot's A coefficient will be a
     synthesized +0.0 and the constant's own SSA value becomes the
     slot's B coefficient (its definition survives as a LUT operand).
     Certification of the value and slot is checked later.  */
  uint32_t bits;
  if (const_leaf_value_p (def, &bits))
    {
      *a = NULL_TREE;
      *b = val;
      *s_mul = nullptr;
      *s_add = nullptr;
      g->leaf_kind[ix] = LUT_LEAF_CONST;
      g->leaf_const_bits[ix] = bits;
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
  /* sfpxcmp is a vector-vector compare, so the boundary is a materialized
     vector and its constant comes from that operand's definition through
     the same value derivation the leaves use.  Require the magnitude on the
     left: sfpi emits `a < b' as sfpxcmp (a, b, LT), and accepting the
     mirrored operand order would read the relation backwards.  */
  if (gimple_call_arg (fcmp, 0) != mag)
    return false;
  tree bound = gimple_call_arg (fcmp, 1);
  if (TREE_CODE (bound) != SSA_NAME)
    return false;
  uint32_t k;
  if (!const_leaf_value_p (SSA_NAME_DEF_STMT (bound), &k))
    return false;
  long mod = rvtt_call_int_arg (fcmp, insnd->mod_arg ());
  if (mod != ((long)(SFPXCMP_MOD1_TYPE_FLOAT << SFPXCMP_MOD1_TYPE_SHIFT)
	      | SFPXCMP_MOD1_CC_LT))
    return false;
  *bits = k;
  return true;
}

/* Insert an SFPLOADI FLOATB of IMM16 (lane value IMM16 << 16, per
   SFPLOADI.md) of type VECTYPE before *GSI at LOC and return its SSA
   value.  Used for the +0.0 coefficient of a degenerate slot and for
   constant-register values with a zero low half.  */

static tree
synth_floatb_coeff (tree vectype, uint32_t imm16, gimple_stmt_iterator *gsi,
		    location_t loc)
{
  const rvtt_insn_data *ld = rvtt_get_insn_data (rvtt_insn_data::sfploadi);
  gcc_assert (ld->decl);
  tree argts[5];
  tree t = TYPE_ARG_TYPES (TREE_TYPE (ld->decl));
  for (int i = 0; i < 5; i++, t = TREE_CHAIN (t))
    argts[i] = TREE_VALUE (t);
  gcall *c = gimple_build_call (ld->decl, 5,
				build_int_cst (argts[0], 0),
				build_int_cst (argts[1], imm16),
				build_int_cst (argts[2], 0),
				build_int_cst (argts[3], 0),
				build_int_cst (argts[4],
					       SFPLOADI_MOD0_FLOATB));
  gimple_call_set_lhs (c, make_ssa_name (vectype));
  gimple_set_location (c, loc);
  gsi_insert_before (gsi, c, GSI_SAME_STMT);
  return gimple_call_lhs (c);
}

/* Return the slot operand to use for coefficient VAL.  The LUT reads its
   table LRegs directly, so a slot operand defined by a constant-register
   read forces a register copy at the use, inside the loop.  Under the leaf
   extension, a read of LReg[9] or LReg[10] whose value is FLOATB-exact is
   replaced by its own SFPLOADI FLOATB, which placement can hoist to the
   preheader.  The original read stays for its other consumers.  Any other
   operand is returned unchanged.  */

static tree
slot_coeff_operand (tree val, tree vectype, gimple_stmt_iterator *gsi,
		    location_t loc)
{
  if (!riscv_tt_opt_lut_select_leaf_ext)
    return val;
  if (!val || TREE_CODE (val) != SSA_NAME)
    return val;
  gcall *def = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (val));
  if (!def)
    return val;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
  if (!insnd || insnd->id != rvtt_insn_data::sfpreadlreg)
    return val;
  uint32_t bits;
  if (!const_leaf_value_p (def, &bits))
    {
      if (dump_file)
	{
	  rvtt_refuse (RVTT_REF_LUT_SLOT_COEFF_VALUE_UNPROVEN, dump_file,
		       "lut-select: slot creg read kept as operand"
		       " (lut-slot-coeff-value-unproven): ");
	  print_gimple_stmt (dump_file, def, 0);
	}
      return val;
    }
  if (bits & 0xffffu)
    {
      if (dump_file)
	{
	  rvtt_refuse (RVTT_REF_LUT_SLOT_COEFF_FLOATB_UNREPRESENTABLE,
		       dump_file,
		       "lut-select: slot creg value %#x not"
		       " FLOATB-exact, operand kept"
		       " (lut-slot-coeff-floatb-unrepresentable): ", bits);
	  print_gimple_stmt (dump_file, def, 0);
	}
      return val;
    }
  if (dump_file)
    fprintf (dump_file, "lut-select: slot creg value %#x materialized as"
	     " FLOATB immediate %#x\n", bits, bits >> 16);
  return synth_floatb_coeff (vectype, bits >> 16, gsi, loc);
}

/* Insert a materialization of the packed 32-bit coefficient WORD before
   *GSI at LOC and return its SSA value.  */

static tree
synth_packed_coeff (tree vectype ATTRIBUTE_UNUSED, tree ptr ATTRIBUTE_UNUSED,
		    uint32_t word, gimple_stmt_iterator *gsi, location_t loc)
{
  /* Build the lowered SFPLOADI sequence rather than the structured
     sfpxloadi: this pass runs after pass_rvtt_immvar_expand, and
     rvtt.md expands a surviving sfpxloadi to a bare FAIL.  */
  auto_vec<gcall *> seq;
  tree res = rvtt_build_loadimm32 (word, &seq);
  for (gcall *c : seq)
    {
      gimple_set_location (c, loc);
      gsi_insert_before (gsi, c, GSI_SAME_STMT);
    }
  return res;
}

/* For a leaf duplicated across two slots, give the second slot its own
   coefficient materialization when the value's definition re-issues
   verbatim (an immediate load, or a read of a hardwired constant
   register); otherwise reuse VAL and let register allocation satisfy
   the second slot with a copy.  Reads of mutable registers are never
   re-issued: re-reading later could observe a different value.  */

static tree
dup_coeff_operand (tree val, gimple_stmt_iterator *gsi)
{
  if (!val || TREE_CODE (val) != SSA_NAME)
    return val;
  gcall *def = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (val));
  if (!def)
    return val;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
  if (!insnd)
    return val;
  if (insnd->id == rvtt_insn_data::sfpreadlreg)
    {
      long creg = rvtt_call_int_arg (def, 0);
      if (creg != 9 && creg != 10)
	return val;
    }
  else if (insnd->id != rvtt_insn_data::sfploadi
	   && insnd->id != rvtt_insn_data::sfpxloadi)
    return val;
  for (unsigned ix = 0; ix != gimple_call_num_args (def); ++ix)
    if (TREE_CODE (gimple_call_arg (def, ix)) != INTEGER_CST)
      return val;

  gcall *copy = as_a <gcall *> (gimple_copy (def));
  gimple_set_vdef (copy, NULL_TREE);
  gimple_set_vuse (copy, NULL_TREE);
  gimple_call_set_lhs (copy, make_ssa_name (TREE_TYPE (val)));
  gsi_insert_before (gsi, copy, GSI_SAME_STMT);
  return gimple_call_lhs (copy);
}

/* Map the group's leaves onto MODE's slots.  Every tree boundary must
   equal one of the mode's architectural boundaries, in order; each
   leaf then covers the consecutive slots between its enclosing
   matched boundaries.  A one-to-one full-arity tree degenerates to
   the identity map.  Returns false (no dump) when the boundaries do
   not embed.  */

static bool
map_slots (const lut_group *g, const rvtt_lut_mode_desc *mode,
	   unsigned *slot_map)
{
  unsigned ti = 0;
  for (unsigned s = 0; s < mode->num_ranges; s++)
    {
      slot_map[s] = ti;
      if (s + 1 < mode->num_ranges && ti < g->num_pred
	  && g->boundary_bits[ti] == mode->boundary_bits[s])
	ti++;
    }
  /* Every tree boundary must match an architectural boundary; one that
     is out of order or not in the mode is never consumed.  */
  return ti == g->num_pred;
}

/* Match the region starting at the sfppushc at GSI up to the matching
   CC-stack balance point, classifying its statements into G, and form
   the LUT if every check passes.  CCR is the function's CC-region tree.
   *CANDIDATE is set once the region is recognizably a magnitude dispatch
   tree; from then on refusals are reported by name.  Returns true when
   the LUT was formed.  */

static bool
match_group (const rvtt_cc_region_tree *ccr, gimple_stmt_iterator gsi,
	     lut_group *g, bool *candidate)
{
  /* Structural cursor: which skeleton statement we expect next inside
     the current predicated region.  */
  enum {
    WANT_XVIF, WANT_FCMP, WANT_CONDB, WANT_ASSIGN,
    WANT_NEXT,		/* after a region's assign: compc or popc */
    WANT_PUSHC,		/* after a compc: the next region's pushc */
    CLOSING		/* draining the popc chain */
  } want = WANT_XVIF;

  *candidate = false;

  /* Structural ceiling for this scan.  Regions beyond the second are only
     ever formable through the fp16 six-entry modes, so without that flag
     the scan stops at two regions.  */
  const unsigned max_pred
    = riscv_tt_opt_lut_select_fp16 ? RVTT_LUT_MAX_RANGES - 1 : 2;
  const unsigned budget = max_pred > 2 ? 160 : 64;

  unsigned region = 0;		/* current predicated region */
  unsigned closed = 0;		/* popcs consumed */

  gimple *stmt = gsi_stmt (gsi);
  gcall *pushc0 = rvtt_call_with_id (stmt, rvtt_insn_data::sfppushc);
  if (!pushc0 || rvtt_call_int_arg (pushc0, 0) != 0)
    return false;
  g->pushc[0] = pushc0;

  /* Statements not yet proven to be leaf computation.  Leaf value
     recognition happens when the live-value assign is reached; any
     statement left unclaimed at the end refuses the group.  */
  auto_vec<gimple *, 16> pending;

  unsigned depth = 1;
  gsi_next (&gsi);
  for (unsigned steps = 0; !gsi_end_p (gsi) && steps < budget;
       gsi_next (&gsi))
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
	case rvtt_insn_data::sfpxpred:
	  if (want == WANT_XVIF)
	    {
	      unsigned expected = ((region + 1)
			   << SFPXPRED_MOD1_DEPTH_SHIFT)
		| SFPXPRED_MOD1_IF | SFPXPRED_MOD1_PUSH;
	      if (region)
		expected |= SFPXPRED_MOD1_ELSE;
	      if (rvtt_call_int_arg (call, 0) != expected
		  || (region == 0
		      ? !integer_zerop (gimple_call_arg (call, 1))
		      : gimple_call_arg (call, 1)
			!= gimple_get_lhs (g->condb[region - 1])))
		return *candidate
		  ? refuse ("lut-structure-mismatch", stmt) : false;
	      g->xvif[region] = stmt;
	      want = WANT_FCMP;
	      continue;
	    }
	  if (want == WANT_NEXT && !g->end
	      && rvtt_call_int_arg (call, 0)
		 == ((region + 1) << SFPXPRED_MOD1_DEPTH_SHIFT)
	      && gimple_call_arg (call, 1)
		 == gimple_get_lhs (g->condb[region]))
	    {
	      g->end = stmt;
	      continue;
	    }
	  return *candidate
	    ? refuse ("lut-structure-mismatch", stmt) : false;

	case rvtt_insn_data::sfpxcmp:
	  if (want != WANT_FCMP)
	    return *candidate
	      ? refuse ("lut-structure-mismatch", stmt) : false;
	  if (region == 0)
	    {
	      /* Candidate identification: a float compare whose compared
		 value is a float abs; sfpxcmp's left operand is the
		 compared value.  From here on refusals are reported.  */
	      tree v = gimple_call_arg (call, 0);
	      if (TREE_CODE (v) == SSA_NAME)
		if (gcall *abs = rvtt_call_with_id (SSA_NAME_DEF_STMT (v),
					       rvtt_insn_data::sfpabs))
		  if (rvtt_call_int_arg (abs, 1) == 1)
		    {
		      *candidate = true;
		      g->mag = v;
		      g->abs_stmt = abs;
		      g->x = gimple_call_arg (abs, 0);
		    }
	      if (!*candidate)
		return false;
	    }
	  if (!match_lt_boundary (call, g->mag, &g->boundary_bits[region]))
	    return refuse ("lut-compare-kind-unsupported", stmt);
	  {
	    /* The boundary's materialization is consumed by this
	       compare and dies with the region.  Claim it only when
	       the compare is its single use; a shared one has to
	       outlive the tree and is not ours to delete.  */
	    tree bnd = gimple_call_arg (call, 1);
	    if (TREE_CODE (bnd) == SSA_NAME && has_single_use (bnd))
	      g->boundary_def[region] = SSA_NAME_DEF_STMT (bnd);
	  }
	  g->fcmp[region] = stmt;
	  want = WANT_CONDB;
	  continue;

	case rvtt_insn_data::sfpxcond:
	  {
	    if (want != WANT_CONDB)
	      return *candidate
		? refuse ("lut-structure-mismatch", stmt) : false;
	    /* The condition tree must be exactly the compare, anchored at
	       this region's vif token.  sfpi emits the condition as
	       sfpxcond (mod, dep, cond), so the token and the condition are
	       arguments 1 and 2.  */
	    tree c = gimple_call_arg (call, 2);
	    tree t = gimple_call_arg (call, 1);
	    if (TREE_CODE (c) != SSA_NAME || TREE_CODE (t) != SSA_NAME
		|| SSA_NAME_DEF_STMT (c) != g->fcmp[region]
		|| SSA_NAME_DEF_STMT (t) != g->xvif[region]
		|| !has_single_use (c) || !has_single_use (t))
	      return refuse ("lut-structure-mismatch", stmt);
	    g->condb[region] = stmt;
	    want = WANT_ASSIGN;
	  }
	  continue;

	case rvtt_insn_data::sfpassign_lv:
	  if (want != WANT_ASSIGN)
	    return *candidate
	      ? refuse ("lut-structure-mismatch", stmt) : false;
	  g->assign[region] = stmt;
	  want = WANT_NEXT;
	  continue;

	case rvtt_insn_data::sfpcompc:
	  if (want != WANT_NEXT)
	    return *candidate
	      ? refuse ("lut-structure-mismatch", stmt) : false;
	  if (region + 1 >= max_pred)
	    /* A further else-branch means the tree partitions more
	       ranges than any reachable capability arity.  */
	    return *candidate
	      ? refuse ("lut-partition-arity-unsupported", stmt) : false;
	  g->compc[region] = stmt;
	  want = WANT_PUSHC;
	  continue;

	case rvtt_insn_data::sfppushc:
	  if (want != WANT_PUSHC || rvtt_call_int_arg (call, 0) != 0)
	    return *candidate
	      ? refuse ("lut-partition-arity-unsupported", stmt) : false;
	  depth++;
	  region++;
	  g->pushc[region] = stmt;
	  want = WANT_XVIF;
	  continue;

	case rvtt_insn_data::sfppopc:
	  {
	    if (!g->end || rvtt_call_int_arg (call, 0) != 0
		|| (want != WANT_NEXT && want != CLOSING))
	      return *candidate
		? refuse ("lut-structure-mismatch", stmt) : false;
	    if (want == WANT_NEXT)
	      {
		/* Region chain ends here: NUM_PRED = region count.
		   Closing right after the FIRST region's assign is a
		   below-arity partition, formable only under the leaf
		   extension's slot duplication.  */
		if (region == 0 && !riscv_tt_opt_lut_select_leaf_ext)
		  return refuse ("lut-partition-arity-unsupported", stmt);
		g->num_pred = region + 1;
		want = CLOSING;
	      }
	    g->popc[closed++] = stmt;
	    depth--;
	    if (closed == g->num_pred)
	      {
		gcc_assert (depth == 0);
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
  /* Cross-check the structure tracked above against the CC-region tree:
     each else-arm frame nested in the previous one, popcs closing
     innermost first, each assign in its own frame, and each frame's
     refinements exactly xvif, fcmp, condb and then compc or end.  A
     mismatch is a bug: assert when checking, otherwise refuse.  */
  {
    bool tree_ok = true;
    rvtt_cc_region *prev = nullptr;
    for (unsigned r = 0; r < g->num_pred && tree_ok; r++)
      {
	rvtt_cc_region *reg = ccr->region_opened_by (g->pushc[r]);
	gimple *rpopc = g->popc[g->num_pred - 1 - r];
	unsigned want_refs = 4;
	tree_ok = reg && ccr->refinements_pure_p (reg)
	  && (r == 0 || reg->parent == prev)
	  && ccr->region_of (g->assign[r]) == reg
	  && reg->exits.length () == 1 && reg->exits[0] == rpopc
	  && reg->refinements.length () == want_refs
	  && reg->refinements[0] == g->xvif[r]
	  && reg->refinements[1] == g->fcmp[r]
	  && reg->refinements[2] == g->condb[r]
	  && reg->refinements[3]
	       == (r + 1 < g->num_pred ? g->compc[r] : g->end);
	prev = reg;
      }
    if (flag_checking)
      gcc_assert (tree_ok);
    if (!tree_ok)
      return refuse ("lut-structure-mismatch", g->pushc[0]);
  }

  /* Recognize the predicated leaves through their live-value assigns:
     assign[ix] = sfpassign_lv (old, new).  The default (last-range)
     leaf reaches the tree as the first assign's incoming live
     value.  */
  unsigned num_pred = g->num_pred;
  unsigned dflt = num_pred;
  tree old0 = gimple_call_arg (as_a <gcall *> (g->assign[0]), 0);
  tree news[RVTT_LUT_MAX_RANGES - 1];
  news[0] = gimple_call_arg (as_a <gcall *> (g->assign[0]), 1);

  for (unsigned r = 1; r < num_pred; r++)
    {
      tree oldr = gimple_call_arg (as_a <gcall *> (g->assign[r]), 0);
      news[r] = gimple_call_arg (as_a <gcall *> (g->assign[r]), 1);
      tree prev = gimple_call_lhs (g->assign[r - 1]);
      if (oldr != prev || !has_single_use (prev))
	return refuse ("lut-structure-mismatch", g->assign[r]);
    }
  g->result = gimple_call_lhs (g->assign[num_pred - 1]);
  if (!g->result)
    return refuse ("lut-structure-mismatch", g->assign[num_pred - 1]);

  for (unsigned r = 0; r < num_pred; r++)
    if (!match_leaf (g, r, news[r], g->mag))
      return refuse ("lut-leaf-not-affine", g->assign[r]);
  if (!match_leaf (g, dflt, old0, g->mag))
    return refuse ("lut-default-leaf-unproven", g->assign[0]);

  /* Leaf values must be consumed only by their assigns, and the
     default leaf only by the tree, so deleting the tree orphans
     nothing.  (A constant leaf's value additionally survives as a LUT
     operand after its assign is deleted.)  */
  for (unsigned r = 0; r < num_pred; r++)
    if (!has_single_use (news[r]))
      return refuse ("lut-leaf-value-escapes", g->assign[r]);
  if (!has_single_use (old0))
    return refuse ("lut-leaf-value-escapes", g->assign[0]);

  /* The predicated leaves' computations must live inside the region
     (between the region's pushc and popc) so their statements are
     accounted for; the default leaf must be defined before the region
     in the same block.  Verify by claiming PENDING.  */
  auto claimed_p = [&] (gimple *stmt) -> bool
    {
      for (unsigned leaf = 0; leaf < num_pred; leaf++)
	if (stmt == g->leaf_mul[leaf] || stmt == g->leaf_add[leaf])
	  return true;
      for (unsigned i = 0; i < num_pred; i++)
	{
	  if (stmt == g->boundary_def[i])
	    return true;
	  /* A boundary that needed both halves is a chained pair: the
	     SFPLOADI root feeds only the claimed SFPLOADI_LV tail, so
	     it is accounted for by the same compare.  */
	  gimple *boundary = g->boundary_def[i];
	  gcall *tail = boundary ? dyn_cast <gcall *> (boundary) : nullptr;
	  const rvtt_insn_data *td = tail ? rvtt_get_insn_data (tail) : nullptr;
	  if (td && td->id == rvtt_insn_data::sfploadi_lv)
	    {
	      tree link = gimple_call_arg (tail, 1);
	      if (TREE_CODE (link) == SSA_NAME
		  && SSA_NAME_DEF_STMT (link) == stmt
		  && has_single_use (link))
		return true;
	    }
	}
      /* Coefficient definitions may sit inside the region when every
	 use is a claimed leaf statement or another coefficient (all
	 pure): they simply outlive the tree as LUT operands.  */
      tree lhs = gimple_call_lhs (stmt);
      if (!lhs)
	return false;
      for (unsigned i = 0; i <= dflt; i++)
	if (lhs == g->a_coeff[i] || lhs == g->b_coeff[i])
	  return true;
      /* The SFPLOADI root of a chained pair is accounted for by whatever
	 claims its SFPLOADI_LV tail -- a coefficient or a compare boundary.
	 Constants whose halves are both significant arrive this way, as
	 immediate lowering runs before this pass.  */
      if (TREE_CODE (lhs) == SSA_NAME)
	{
	  gimple *use_stmt = nullptr;
	  use_operand_p use_p;
	  if (single_imm_use (lhs, &use_p, &use_stmt))
	    {
	      gcall *tail = dyn_cast <gcall *> (use_stmt);
	      const rvtt_insn_data *td
		= tail ? rvtt_get_insn_data (tail) : nullptr;
	      if (td && td->id == rvtt_insn_data::sfploadi_lv)
		{
		  for (unsigned i = 0; i < num_pred; i++)
		    if (tail == g->boundary_def[i])
		      return true;
		  tree tlhs = gimple_call_lhs (tail);
		  if (tlhs)
		    for (unsigned i = 0; i <= dflt; i++)
		      if (tlhs == g->a_coeff[i] || tlhs == g->b_coeff[i])
			return true;
		}
	    }
	}
      return false;
    };
  for (gimple *stmt : pending)
    if (!claimed_p (stmt))
      return refuse ("lut-region-foreign-stmt", stmt);

  for (int leaf = dflt; leaf >= 0; leaf--)
    {
      gimple *stmts[2] = { g->leaf_mul[leaf], g->leaf_add[leaf] };
      for (gimple *stmt : stmts)
	if (stmt && gimple_bb (stmt) != gimple_bb (g->pushc[0]))
	  return refuse (leaf == (int) dflt ? "lut-default-leaf-unproven"
			 : "lut-leaf-value-escapes", stmt);
    }

  /* Trailing sign-restore fold candidate: the result's single non-debug
     consumer is a default-mode sfpsetsgn_v copying the sign of the LUT
     input X onto it.  */
  gimple *sgn_use = nullptr;
  {
    gimple *use_stmt = nullptr;
    unsigned n_uses = 0;
    imm_use_iterator iter;
    gimple *use;
    FOR_EACH_IMM_USE_STMT (use, iter, g->result)
      if (!is_gimple_debug (use))
	{
	  use_stmt = use;
	  if (++n_uses > 1)
	    break;
	}
    if (n_uses == 1)
      if (gcall *sgn = rvtt_call_with_id (use_stmt,
					  rvtt_insn_data::sfpsetsgn_v))
	if (gimple_call_arg (sgn, 0) == g->result
	    && gimple_call_arg (sgn, 1) == g->x
	    && rvtt_call_int_arg (sgn, 2) == 0
	    && gimple_call_lhs (sgn))
	  sgn_use = sgn;
  }

  /* Find a target mode whose boundaries embed the tree's boundaries in
     order, with the required sign behaviour and enabled gate.  A fold
     candidate with no sign-restore mode falls back to the sign-update
     mode and keeps the explicit sign copy.  Modes are tried in table
     order, so the FP32 3-entry table is preferred when it fits.  */
  const rvtt_lut_mode_desc *table = nullptr;
  unsigned n_modes = rvtt_lut_modes (&table);
  if (!n_modes)
    return refuse ("lut-no-target-capability", g->pushc[0]);

  /* On Wormhole, SFPABS keeps the sign of a negative NaN and the compare
     inherits it, so the tree takes range 0 while the LUT buckets the input
     (e.g. 0xff800001) into the top range.  Only -ffinite-math-only rules
     those inputs out.  */
  if (TARGET_XTT_TENSIX_WH && !flag_finite_math_only)
    return refuse ("lut-wh-negative-nan-divergent", g->pushc[0]);

  const rvtt_lut_mode_desc *mode = nullptr;
  unsigned slot_map[RVTT_LUT_MAX_RANGES];
  bool capability_seen = false;
  bool below_arity_blocked = false;
  for (int sgn_pass = sgn_use ? 1 : 0; sgn_pass >= 0 && !mode; sgn_pass--)
    {
      bool want_sgn = sgn_pass == 1;
      for (unsigned m = 0; m < n_modes; m++)
	{
	  const rvtt_lut_mode_desc *cand = &table[m];
	  if (cand->sign_restore != want_sgn)
	    continue;
	  if (cand->gate && *cand->gate <= 0)
	    continue;
	  if (num_pred + 1 > cand->num_ranges)
	    continue;
	  if (num_pred + 1 < cand->num_ranges
	      && !riscv_tt_opt_lut_select_leaf_ext)
	    {
	      /* Below this mode's arity: duplication is a leaf-ext
		 capability.  */
	      below_arity_blocked = true;
	      continue;
	    }
	  capability_seen = true;
	  if (!map_slots (g, cand, slot_map))
	    continue;
	  mode = cand;
	  if (!want_sgn)
	    sgn_use = nullptr;
	  break;
	}
    }
  if (!mode)
    {
      if (capability_seen)
	return refuse ("lut-boundary-mismatch", g->fcmp[0]);
      return refuse (below_arity_blocked ? "lut-partition-arity-unsupported"
		     : "lut-no-target-capability", g->pushc[0]);
    }

  /* Each non-affine leaf class must be certified bit-exact for its slot
     on this target under the current finite-math setting, and a
     constant leaf's value must be in a certified value class.  */
  for (unsigned s = 0; s < mode->num_ranges; s++)
    {
      unsigned leaf = slot_map[s];
      bool tail = s == mode->num_ranges - 1;
      gimple *at = leaf < num_pred ? g->assign[leaf] : g->assign[0];
      switch (g->leaf_kind[leaf])
	{
	case LUT_LEAF_AFFINE:
	  break;
	case LUT_LEAF_MUL0:
	  if (!rvtt_lut_leaf_class_certified_p (RVTT_LUT_LEAF_MUL0, tail,
						flag_finite_math_only))
	    return refuse ("lut-leaf-bitexact-unproven", at);
	  break;
	case LUT_LEAF_CONST:
	  if (!rvtt_lut_const_value_certified_p (g->leaf_const_bits[leaf])
	      || !rvtt_lut_leaf_class_certified_p (RVTT_LUT_LEAF_CONST, tail,
						   flag_finite_math_only))
	    return refuse ("lut-leaf-bitexact-unproven", at);
	  break;
	}
    }

  /* The FP32 3-entry table holds any FP32 value verbatim.  The
     LUT16-packed modes need every slot coefficient's compile-time value
     and its exact re-encoding, packed into three A-words and three
     B-words.  */
  uint32_t packed_a[3], packed_b[3];
  if (mode->coeff_encoding == RVTT_LUT_COEFF_LUT16_PACKED)
    {
      gcc_assert (mode->num_ranges == 6);
      uint16_t enc_a[6], enc_b[6];
      for (unsigned s = 0; s < mode->num_ranges; s++)
	{
	  unsigned leaf = slot_map[s];
	  gimple *at = leaf < num_pred ? g->assign[leaf] : g->assign[0];
	  uint32_t a_bits = 0, b_bits = 0;
	  switch (g->leaf_kind[leaf])
	    {
	    case LUT_LEAF_AFFINE:
	      if (!const_leaf_value_p (SSA_NAME_DEF_STMT (g->a_coeff[leaf]),
				       &a_bits)
		  || !const_leaf_value_p (SSA_NAME_DEF_STMT
					  (g->b_coeff[leaf]), &b_bits))
		return refuse ("lut-coeff-value-unproven", at);
	      break;
	    case LUT_LEAF_MUL0:
	      if (!const_leaf_value_p (SSA_NAME_DEF_STMT (g->a_coeff[leaf]),
				       &a_bits))
		return refuse ("lut-coeff-value-unproven", at);
	      b_bits = 0;
	      break;
	    case LUT_LEAF_CONST:
	      a_bits = 0;
	      b_bits = g->leaf_const_bits[leaf];
	      break;
	    }
	  if (!rvtt_lut16_encode_exact_p (a_bits, &enc_a[s])
	      || !rvtt_lut16_encode_exact_p (b_bits, &enc_b[s]))
	    return refuse ("lut-coeff-encoding-unrepresentable", at);
	}
      for (unsigned j = 0; j < 3; j++)
	{
	  packed_a[j] = (uint32_t) enc_a[2 * j]
	    | ((uint32_t) enc_a[2 * j + 1] << 16);
	  packed_b[j] = (uint32_t) enc_b[2 * j]
	    | ((uint32_t) enc_b[2 * j + 1] << 16);
	}
    }
  else
    {
      /* A synthesized +0.0 must be representable in the mode's
	 coefficient encoding; only a non-FP32 mode could reject it.  */
      for (unsigned s = 0; s < mode->num_ranges; s++)
	if (g->leaf_kind[slot_map[s]] != LUT_LEAF_AFFINE
	    && mode->coeff_encoding != RVTT_LUT_COEFF_FP32_DIRECT)
	  return refuse ("lut-coeff-encoding-unrepresentable",
			 g->assign[0]);
    }

  /* Materialize.  */
  const rvtt_insn_data *lut_insnd = rvtt_get_insn_data (mode->insn);
  gcc_assert (lut_insnd->decl);

  gimple *anchor = g->assign[num_pred - 1];
  gimple_stmt_iterator rsi = gsi_for_stmt (anchor);
  location_t loc = gimple_location (anchor);
  tree vectype = TREE_TYPE (g->result);

  /* Slot operands.  The packed modes take freshly materialized packed
     words; the FP32-direct modes take the leaves' own coefficients, with
     a synthesized +0.0 for missing ones and a fresh copy for a
     duplicated leaf's later slots where possible.  */
  tree aops[3], bops[3];
  if (mode->coeff_encoding == RVTT_LUT_COEFF_LUT16_PACKED)
    {
      tree ptr = gimple_call_arg (as_a <gcall *> (g->fcmp[0]), 0);
      for (unsigned j = 0; j < 3; j++)
	{
	  aops[j] = synth_packed_coeff (vectype, ptr, packed_a[j],
					&rsi, loc);
	  bops[j] = synth_packed_coeff (vectype, ptr, packed_b[j],
					&rsi, loc);
	}
    }
  else
    {
      bool slot_seen[RVTT_LUT_MAX_RANGES] = {};
      for (unsigned s = 0; s < mode->num_ranges; s++)
	{
	  unsigned leaf = slot_map[s];
	  tree a = g->a_coeff[leaf];
	  tree b = g->b_coeff[leaf];
	  if (slot_seen[leaf])
	    {
	      a = dup_coeff_operand (a, &rsi);
	      b = dup_coeff_operand (b, &rsi);
	    }
	  slot_seen[leaf] = true;
	  a = slot_coeff_operand (a, vectype, &rsi, loc);
	  b = slot_coeff_operand (b, vectype, &rsi, loc);
	  aops[s] = a ? a : synth_floatb_coeff (vectype, 0, &rsi, loc);
	  bops[s] = b ? b : synth_floatb_coeff (vectype, 0, &rsi, loc);
	}
    }

  gcall *lut = gimple_build_call
    (lut_insnd->decl, 8,
     aops[0], aops[1], aops[2],
     bops[0], bops[1], bops[2],
     g->x, build_int_cst (unsigned_type_node, mode->mod0));
  gimple_call_set_lhs (lut, g->result);
  gimple_set_location (lut, loc);

  rsi = gsi_for_stmt (anchor);
  gsi_replace (&rsi, lut, false);

  if (dump_file)
    {
      static const char *const kind_names[]
	= { "affine", "mul0", "const" };
      fprintf (dump_file,
	       "lut-select: formed %s (mod0 %#x) from %u-range magnitude"
	       " dispatch tree, boundaries ",
	       mode->name, mode->mod0, num_pred + 1);
      for (unsigned s = 0; s + 1 < mode->num_ranges; s++)
	fprintf (dump_file, "%s%#x", s ? "," : "", mode->boundary_bits[s]);
      fprintf (dump_file, ", slot leaves ");
      for (unsigned s = 0; s < mode->num_ranges; s++)
	fprintf (dump_file, "%s%s", s ? "," : "",
		 kind_names[g->leaf_kind[slot_map[s]]]);
      fprintf (dump_file, ": ");
      print_gimple_stmt (dump_file, lut, 0);
    }

  /* Delete the tree: users before definers so nothing is orphaned.
     Coefficient definitions stay when they now feed the LUT
     (FP32-direct modes); under a packed mode they die with the tree
     and are swept below once use-free.  */
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

  /* Record the coefficient definitions before the leaves disappear
     (packed modes only: their SSA values are not LUT operands).  */
  auto_vec<gimple *, 12> dead_coeff_defs;
  if (mode->coeff_encoding == RVTT_LUT_COEFF_LUT16_PACKED)
    for (unsigned i = 0; i <= dflt; i++)
      {
	tree ops[2] = { g->a_coeff[i], g->b_coeff[i] };
	for (tree op : ops)
	  if (op && TREE_CODE (op) == SSA_NAME)
	    {
	      gimple *def = SSA_NAME_DEF_STMT (op);
	      if (def && rvtt_get_insn_data (def)
		  && !dead_coeff_defs.contains (def))
		{
		  dead_coeff_defs.safe_push (def);
		  /* A packed FP16 coefficient can be a chained
		     SFPLOADI + SFPLOADI_LV materialization.  Deleting the
		     tail strips the single-use root's lhs, but the root call
		     is volatile and would survive as a result-less
		     instruction, so record it now.  Tails precede roots, so
		     the liveness recheck below keeps a shared root.  */
		  gcall *call = dyn_cast <gcall *> (def);
		  if (gcall *root
		      = call ? rvtt_chained_loadi_root (call) : nullptr)
		    if (!dead_coeff_defs.contains (root))
		      dead_coeff_defs.safe_push (root);
		}
	    }
      }

  /* Likewise record the boundary materializations, which the matcher
     claimed only when the compare was their single use; otherwise the
     volatile SFPLOADI calls would survive without a result.  Tails
     precede roots so deleting a tail exposes its root as dead.  */
  auto_vec<gimple *, 10> dead_boundary_defs;
  for (unsigned i = 0; i < num_pred; ++i)
    if (gimple *tail = g->boundary_def[i])
      {
	if (!dead_boundary_defs.contains (tail))
	  dead_boundary_defs.safe_push (tail);
	gcall *call = dyn_cast <gcall *> (tail);
	if (gcall *root = call ? rvtt_chained_loadi_root (call) : nullptr)
	  if (!dead_boundary_defs.contains (root))
	    dead_boundary_defs.safe_push (root);
      }

  /* A folded sign restore dissolves: the LUT's mode word already
     copies the input's sign, so the copy's consumers take the LUT
     value directly.  */
  if (sgn_use)
    {
      tree sgn_lhs = gimple_call_lhs (sgn_use);
      if (dump_file)
	{
	  fprintf (dump_file,
		   "lut-select: folded trailing sign restore into the LUT"
		   " mode word: ");
	  print_gimple_stmt (dump_file, sgn_use, 0);
	}
      replace_uses_by (sgn_lhs, g->result);
      remove (sgn_use);
    }

  for (unsigned r = 0; r + 1 < num_pred; r++)
    remove (g->assign[r]);	/* assign[num_pred-1] was replaced.  */
  for (unsigned r = 0; r < num_pred; r++)
    {
      if (r == 0)
	remove (g->end);
      remove (g->condb[r]);
      remove (g->popc[r]);
      remove (g->xvif[r]);
      remove (g->fcmp[r]);
      remove (g->pushc[r]);
      if (r + 1 < num_pred)
	remove (g->compc[r]);
    }
  for (int leaf = dflt; leaf >= 0; leaf--)
    {
      remove (g->leaf_add[leaf]);
      remove (g->leaf_mul[leaf]);
    }
  /* Recheck liveness in case a boundary value is shared.  */
  for (gimple *def : dead_boundary_defs)
    {
      if (!gimple_bb (def))
	continue;
      tree lhs = gimple_call_lhs (def);
      if (!lhs || (TREE_CODE (lhs) == SSA_NAME && has_zero_uses (lhs)))
	remove (def);
    }
  /* Under a packed mode the original coefficient materializations are
     dead once their leaves are gone (unless something else still uses
     them, in which case they stay).  Chained tails precede their roots
     in DEAD_COEFF_DEFS, so removing a tail first exposes an unshared
     root as dead.  A null lhs means the recursive single-use cleanup in
     the leaf deletions above already stripped the definition's result:
     the load is equally dead.  */
  for (gimple *def : dead_coeff_defs)
    {
      if (!gimple_bb (def))
	continue;
      tree lhs = gimple_call_lhs (def);
      if (!lhs || (TREE_CODE (lhs) == SSA_NAME && has_zero_uses (lhs)))
	remove (def);
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

static unsigned n_placed;

/* Move the in-loop coefficient materializations of LUT to the loop
   preheader, using the invariant-loadi pass's legality checks
   (rvtt-macro-ownership.h).  The LREG-budget check covers the whole
   coefficient set: either all qualifying coefficients move or none do.
   Nothing is edited until every check has passed.  */

static void
place_coefficients (gcall *lut)
{
  basic_block bb = gimple_bb (lut);
  class loop *loop = bb->loop_father;
  if (!loop || !loop_outer (loop))
    return;	/* Not inside a loop: nothing to place.  */

  auto keep = [] (const char *reason)
    {
      if (dump_file)
	fprintf (dump_file, "lut-select: coefficients kept in loop (%s)\n",
		 reason);
    };

  edge entry = rvtt_loop_entry_edge (loop);
  if (!entry)
    return keep ("lut-coefficient-loop-multi-entry");
  if (rvtt_loop_hoist_region_opaque_p (loop, entry))
    return keep ("lut-coefficient-region-opaque");
  if (rvtt_preheader_insertion_blocked_p (entry))
    return keep ("lut-coefficient-preheader-blocked");
  if (rvtt_loop_has_sfpu_barrier_p (loop))
    return keep ("lut-coefficient-loop-barrier");
  if (expected_loop_iterations_unbounded (loop) < 1)
    return keep ("lut-coefficient-loop-cold");
  if (!rvtt_stmt_executes_every_entered_iteration_p (loop, bb))
    return keep ("lut-coefficient-conditional-row");

  /* An architectural LREG write must never be speculated out of a
     possibly-zero-trip loop.  A candidate qualifies if the first
     iteration provably executes or if it sits in the loop header, every
     statement of which runs as soon as the loop is entered (counted
     loops are rotated by now, so the first test rarely applies).  */
  bool first_iteration = rvtt_loop_first_iteration_executes_p (loop, entry);

  /* Collect the in-loop coefficient definitions that are invariant
     immediates executing on every entered iteration.  Others stay
     where they are; only the LREG budget below is all-or-none.  */
  auto_vec<gcall *> coeffs;
  for (unsigned ix = 0; ix < 6; ix++)
    {
      tree op = gimple_call_arg (lut, ix);
      if (TREE_CODE (op) != SSA_NAME)
	continue;
      gimple *def = SSA_NAME_DEF_STMT (op);
      basic_block def_bb = gimple_bb (def);
      if (!def_bb || !flow_bb_inside_loop_p (loop, def_bb))
	continue;
      gcall *call = dyn_cast <gcall *> (def);
      /* Under the leaf extension a read of the read-only LReg[9] or
	 LReg[10] is as placeable as an invariant immediate.  */
      auto const_creg_read_p = [] (gcall *c) -> bool
	{
	  if (!riscv_tt_opt_lut_select_leaf_ext)
	    return false;
	  const rvtt_insn_data *insnd = rvtt_get_insn_data (c);
	  if (!insnd || insnd->id != rvtt_insn_data::sfpreadlreg)
	    return false;
	  tree arg = gimple_call_arg (c, 0);
	  if (TREE_CODE (arg) != INTEGER_CST)
	    return false;
	  HOST_WIDE_INT creg = tree_to_shwi (arg);
	  return creg == 9 || creg == 10;
	};
      if (!call
	  || !(rvtt_invariant_constant_load_p (call, loop,
					       /*allow_shortened=*/true)
	       || const_creg_read_p (call))
	  || !rvtt_stmt_executes_every_entered_iteration_p (loop, def_bb)
	  || (def_bb != loop->header && !first_iteration))
	{
	  if (dump_file)
	    {
	      rvtt_refuse (RVTT_REF_LUT_COEFFICIENT_UNPROVEN, dump_file,
			   "lut-select: coefficient stays in loop"
			   " (lut-coefficient-unproven): ");
	      print_gimple_stmt (dump_file, def, 0);
	    }
	  continue;
	}
      if (!coeffs.contains (call))
	coeffs.safe_push (call);
    }
  if (coeffs.is_empty ())
    return;

  /* Either every coefficient fits live across the loop in the eight-LREG
     file or none moves.  For the FP16 packed modes (mod0 bit 1) and under
     the leaf extension, reads of the constant registers (LReg[8..14]) are
     not counted: the operands around the LUT accept the constant-register
     class directly, so they do not occupy an allocatable LREG.  A constant
     read feeding a LUT table slot is still counted, since it needs a real
     copy (creg_resident_p in rvtt-pressure.cc).  */
  bool fp16_mode = (rvtt_call_int_arg (lut, 7) & 2) != 0;
  bool creg_exempt = fp16_mode || riscv_tt_opt_lut_select_leaf_ext;
  if (!rvtt_pressure_loop_legal_p (loop, coeffs, /*report=*/true,
				   /*cc_transients=*/false,
				   /*exempt_creg_reads=*/creg_exempt))
    return keep ("lut-coefficient-pressure");

  basic_block preheader = rvtt_commit_hoist_preheader (entry);
  for (gcall *call : coeffs)
    {
      gcall *root = rvtt_chained_loadi_root (call);
      if (tree vdef = gimple_vdef (call))
	{
	  if (TREE_CODE (vdef) == SSA_NAME)
	    {
	      unlink_stmt_vdef (call);
	      release_ssa_name (vdef);
	    }
	  gimple_set_vdef (call, NULL_TREE);
	}
      if (gimple_vuse (call))
	{
	  gimple_set_vuse (call, NULL_TREE);
	  update_stmt (call);
	}
      if (root)
	{
	  if (tree vdef = gimple_vdef (root))
	    {
	      if (TREE_CODE (vdef) == SSA_NAME)
		{
		  unlink_stmt_vdef (root);
		  release_ssa_name (vdef);
		}
	      gimple_set_vdef (root, NULL_TREE);
	    }
	  if (gimple_vuse (root))
	    {
	      gimple_set_vuse (root, NULL_TREE);
	      update_stmt (root);
	    }
	  gimple_stmt_iterator root_from = gsi_for_stmt (root);
	  gsi_move_to_bb_end (&root_from, preheader);
	}
      gimple_stmt_iterator from = gsi_for_stmt (call);
      gsi_move_to_bb_end (&from, preheader);
      n_placed++;
      if (dump_file)
	{
	  fprintf (dump_file,
		   "lut-select: placed coefficient materialization in loop"
		   " preheader bb %d: ", preheader->index);
	  print_gimple_stmt (dump_file, call, 0);
	}
    }
}

/* Walk FUN for structured CC regions opened by an sfppushc and form a
   LUT instruction from every matched range-dispatch group.  Each
   formed LUT call is appended to FORMED for the caller's coefficient
   placement.  Returns whether the IL changed.  */

static bool
transform (function *fun, auto_vec<gcall *> *formed)
{
  bool changed = false;
  basic_block bb;
  /* The CC-region tree is computed once: a formation deletes whole
     frames and inserts only CC-inert statements, so the remaining
     frames stay valid.  */
  rvtt_cc_region_tree ccr (fun);
  FOR_EACH_BB_FN (bb, fun)
    {
      gimple_stmt_iterator gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gimple_stmt_iterator next = gsi;
	  gsi_next (&next);
	  if (rvtt_call_with_id (gsi_stmt (gsi), rvtt_insn_data::sfppushc))
	    {
	      lut_group g;
	      bool candidate;
	      if (match_group (&ccr, gsi, &g, &candidate))
		{
		  changed = true;
		  /* The group's statements are gone; restart after the
		     formed LUT.  */
		  gimple *lut = SSA_NAME_DEF_STMT (g.result);
		  formed->safe_push (as_a <gcall *> (lut));
		  next = gsi_for_stmt (lut);
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
  OPTGROUP_OTHER,
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
    n_placed = 0;
    if (TARGET_XTT_TENSIX_QSR)
      {
	rvtt_refuse (RVTT_REF_LUT_NO_TARGET_CAPABILITY, dump_file,
		     "lut-select: refused (lut-no-target-capability):"
		     " QSR has no validated LUT capability\n");
	return 0;
      }
    auto_vec<gcall *> formed;
    bool changed = transform (fun, &formed);
    if (!formed.is_empty ())
      {
	    /* Place coefficients only in loops where a LUT was formed.  */
	loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
	if (!dom_info_available_p (CDI_DOMINATORS))
	  calculate_dominance_info (CDI_DOMINATORS);
	for (gcall *lut : formed)
	  place_coefficients (lut);
	loop_optimizer_finalize ();
      }
    if (dump_file)
      {
	fprintf (dump_file, "lut-select: groups=%u refusals=%u\n",
		 n_formed, n_refused);
	fprintf (dump_file, "lut-select: placements=%u\n", n_placed);
      }
    return changed ? TODO_update_ssa_only_virtuals : 0;
  }
};

} /* anonymous namespace */

/* The pass must run while the structured condition form is still
   explicit, before pass_rvtt_vif lowers it to raw CC operations.  */

gimple_opt_pass *
make_pass_rvtt_lut_select (gcc::context *ctxt)
{
  return new pass_rvtt_lut_select (ctxt);
}
