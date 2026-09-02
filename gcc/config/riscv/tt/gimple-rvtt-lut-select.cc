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
     ON BLACKHOLE, for the proven non-negative magnitude these order
     identically to the tree's float less-than compares, including for
     NaN (bucket num_ranges-1 on both paths) and infinities.  ON
     WORMHOLE this bucket agreement FAILS for negative-NaN inputs
     (SFPABS keeps the -NaN sign; the WH compare-subtract inherits the
     operand's sign into its NaN result), so WH formation is admitted
     only under the function's -ffinite-math-only license and refuses
     lut-wh-negative-nan-divergent otherwise -- see the certification
     record in rvtt-lut-tables.cc and the guard at the capability
     check below;
   - the hardware evaluates fma (A_i, |x|, B_i) with a single
     rounding; the tree's separate mul+add pair is already fused into
     the same single-rounding SFPMAD by the default-on rvtt combine
     pass, so selection introduces no new rounding difference relative
     to the default pipeline;
   - lanes disabled by an enclosing condition context are untouched by
     both forms (the LUT executes under the same enclosing CC that
     gated the tree's leaf writes).

   Second increment:

   - Trailing sign-restore folding: when the selected value's single
     consumer copies the LUT input's own sign back onto it (the vector
     sign-copy instruction in its default mode), and the capability
     table provides a sign-restore execution mode, the copy folds into
     the LUT's mode word and the explicit instruction dissolves.  Any
     other consumer, operand order, sign source, or mode keeps the
     explicit instruction.

   - Coefficient placement: with the dispatch tree's CC scaffolding
     dissolved, the formed LUT's coefficient materializations are
     ordinary loop-invariant immediates.  The early invariant-loadi
     pass necessarily refused them (pre-formation the loop body
     manipulates lane-enable CC state), so formation re-runs the same
     shared preheader-hoist proofs, scoped to exactly the loops where a
     LUT formed this execution.  The placement is transactional against
     the architectural eight-LREG budget: either every in-loop
     coefficient materialization moves to the preheader or none does,
     so a refusal leaves the bytes exactly at the formation-only
     shape.

   Third increment (-mtt-tensix-optimize-lut-select-leaf-ext,
   default-off):

   - Certified leaf classes beyond the affine leaf.  A multiply-only
     leaf becomes a slot with a synthesized +0.0 B coefficient, and a
     compile-time-provable constant leaf (an immediate materialization
     or a read of a hardwired constant register) becomes a slot with a
     synthesized +0.0 A coefficient.  Admission is NOT structural
     analogy: each (leaf class, slot partition, target) combination is
     admitted only when rvtt-lut-tables.cc records its exhaustive
     bit-exact certification against the pinned typed LUT semantics --
     inf/NaN inputs included -- and refuses
     (lut-leaf-bitexact-unproven) otherwise.  In particular a constant
     leaf in the TAIL slot diverges from the tree on exactly the
     inf/NaN inputs (the slot computes fma (+0.0, |x|, C) = NaN where
     the tree's untouched default lanes keep C), so it is admitted
     only under the function's own -ffinite-math-only license.
     Constant values outside the certified value classes (negative
     zero, denormals, NaNs) refuse by the same name; constants whose
     value cannot be derived from the audited materialization forms
     refuse lut-leaf-not-affine as before.

   - Below-arity partitions.  A tree with fewer leaves than a mode's
     arity, every one of whose boundaries equals one of the mode's
     architectural boundaries, forms by DUPLICATING each leaf across
     the consecutive slots it spans -- slots holding identical
     coefficients evaluate the identical fma, so the per-slot leaf
     certifications carry the whole argument.  The duplicated leaf
     must be admissible for EACH slot it covers (in particular its
     tail copy is still subject to the tail rules above).  Any other
     boundary refuses lut-boundary-mismatch; arities above every
     mode's stay lut-partition-arity-unsupported.

   - Coefficient encoding.  The FP32 3-entry table holds any FP32
     coefficient verbatim; the FP16 modes below have a narrower
     coefficient encoding and prove each compile-time coefficient
     re-encodes exactly or refuse lut-coeff-encoding-unrepresentable.

   Fourth increment (-mtt-tensix-optimize-lut-select-fp16,
   default-off):

   - The FP16-coefficient six-entry SFPLUTFP32 table modes
     (TABLE1/TABLE2).  A six-range magnitude dispatch tree over the
     architectural 0.5/1.0/1.5/2.0/{3.0,4.0} boundaries (or a
     below-arity tree on a boundary subset, under the leaf extension)
     forms one SFPLUTFP32 whose six coefficient LRegs each pack two
     LUT16-encoded coefficients.  Because the packed halves must be
     assembled at compile time, EVERY slot coefficient must be a
     compile-time-provable constant (the same audited materialization
     forms the constant-leaf derivation uses); an unprovable
     coefficient refuses lut-coeff-value-unproven, and a value that
     does not re-encode exactly in the LUT16 format refuses
     lut-coeff-encoding-unrepresentable.  The formed instruction then
     evaluates bit-for-bit the same fma the source leaf's MAD
     computed, so the whole equivalence argument reduces to the
     six-way bucket-agreement certification recorded in
     rvtt-lut-tables.cc (Blackhole: all 2^32 inputs, both tables;
     Wormhole: behind the -ffinite-math-only guard shared with the
     base increment).  The original per-leaf coefficient
     materializations die with the tree; the packed words are
     synthesized as ordinary invariant immediates and participate in
     the shared preheader placement.  */

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
#include "rvtt-pressure.h"
#include "rvtt-refuse.h"
#include "rvtt-lut-tables.h"
#include "rvtt-macro-ownership.h"
#include "rvtt-cc-region.h"

namespace {

/* Architectural ceiling over every table row's arity.  */
#define RVTT_LUT_MAX_RANGES 6

/* Leaf classes the matcher recognizes.  AFFINE is the base class; the
   others exist only under the leaf-extension flag and admit only
   against the certification table.  */
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

/* The 32-bit lane value of a provable constant definition, or false.
   The audited forms and their value reconstructions follow the
   prgm-const residency discipline (gimple-rvtt-prgm-const.cc
   constant_chain_value_p): the 32-bit sfpxloadi forms carry the
   pattern verbatim, the shortened SFPLOADI FLOATB form is imm16 << 16,
   the shortened SFPLOADI FLOATA form is the architectural fp16
   exponent-rebias (SFPLOADI.md; the reference simulator's
   TENSIX_EXECUTE_SFPLOADI case 1, verbatim:
   (s << 16) | ((em + (112 << 10)) << 13) -- no special cases), and a
   read of a hardwired constant register carries
   that register's architectural value (LReg.md: LReg[9] is read-only
   all-lanes zero, LReg[10] is read-only all-lanes 1.0; the
   reference simulator's SFPADD/SFPMUL executors enforce the same two
   constants).  Every other form refuses: its value is not on record here.  */

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
      long creg = int_arg (call, 0);
      if (creg == 9)
	*bits = 0x00000000u;
      else if (creg == 10)
	*bits = 0x3f800000u;
      else
	return false;
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
  if (add && int_arg (add, 2) == 0)
    /* One add operand must be a single-use mul by MAG; the other is
       B.  */
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

  if (!riscv_tt_opt_lut_select_leaf_ext)
    return false;

  /* Mul-only leaf: the slot's B coefficient will be a synthesized
     +0.0 (certified: adding the exact zero to the partially fused
     product leaves the standalone multiply, rvtt-lut-tables.cc).  */
  if (gcall *mul = is_rvtt_call (def, rvtt_insn_data::sfpmul))
    if (int_arg (mul, 2) == 0)
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
     Whether the VALUE's class and the slot's partition are certified
     is decided at admission, with named refusals.  */
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

/* Synthesize a shortened-SFPLOADI-FLOATB coefficient materialization
   (lane value IMM16 << 16 -- SFPLOADI.md FLOATB, exact for any FP32
   pattern with a zero low half) before *GSI and return its SSA value.
   The synthesized +0.0 (imm16 0) is the certified degenerate-slot
   coefficient; the leaf extension also uses this form to give a
   FLOATB-exact constant-register value its own slot word.  The
   synthesized load is an ordinary in-loop invariant immediate; the
   shared placement proofs may later move it to the preheader.  */

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

/* A LUT table slot is an implicit hard register: the formed
   instruction reads the architectural table LRegs directly, so a slot
   operand defined by a constant-register read forces a physical copy
   into the slot LReg at register allocation -- and the allocator
   inserts that copy at the USE, inside the row loop (the tanhderivlut
   5th loop word, a named measured residual).  Under the leaf extension,
   when the read is of a hardwired constant register whose
   architectural value is on record (const_leaf_value_p: LReg[9] zero,
   LReg[10] one) AND that value is FLOATB-exact (low 16 bits zero --
   SFPLOADI.md: FLOATB materializes imm16 << 16 bit-exactly), the slot
   coefficient becomes its own shortened-FLOATB materialization like
   every other slot word: an invariant immediate the shared placement
   moves to the preheader, leaving the row loop at the hand kernel's
   word count.  The creg read itself stays for its creg-capable
   consumers (a mad addend reads LReg[10] directly).  Anything else --
   a mutable or unrecorded register, or a value that does not re-encode
   exactly in FLOATB -- keeps the historical operand by name, byte
   shape unchanged.  */

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
	  rvtt_refuse (RVTT_REF_LUT_SLOT_COEFF_FLOATB_UNREPRESENTABLE, dump_file,
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

/* Synthesize a packed 32-bit coefficient-word materialization (the
   audited full sfpxloadi form, raw 32-bit lane pattern) before *GSI
   and return its SSA value.  PTR is the instruction-buffer operand
   lifted from the group's own compare (every sfpi-shaped call site
   carries it as argument 0).  The synthesized load is an ordinary
   invariant immediate for the shared preheader placement.  */

static tree
synth_packed_coeff (tree vectype, tree ptr, uint32_t word,
		    gimple_stmt_iterator *gsi, location_t loc)
{
  const rvtt_insn_data *ld = rvtt_get_insn_data (rvtt_insn_data::sfpxloadi);
  gcc_assert (ld->decl);
  tree argts[5];
  tree t = TYPE_ARG_TYPES (TREE_TYPE (ld->decl));
  for (int i = 0; i < 5; i++, t = TREE_CHAIN (t))
    argts[i] = TREE_VALUE (t);
  gcall *c = gimple_build_call (ld->decl, 5,
				ptr,
				build_int_cst (argts[1], word),
				build_int_cst (argts[2], 0),
				build_int_cst (argts[3], 0),
				build_int_cst (argts[4], -32));
  gimple_call_set_lhs (c, make_ssa_name (vectype));
  gimple_set_location (c, loc);
  gsi_insert_before (gsi, c, GSI_SAME_STMT);
  return gimple_call_lhs (c);
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
      long creg = int_arg (def, 0);
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
  /* Every tree boundary must have been consumed by exactly one
     architectural boundary.  (The boundaries arrive in program order;
     an out-of-order or foreign boundary is simply never consumed.)  */
  return ti == g->num_pred;
}

/* Collect the contiguous region starting at the sfppushc at *GSI up
   to the matching CC-stack balance point, classifying its statements
   into G.  Returns true when the full structural skeleton matched.
   REPORT enables named refusals (set once the region has identified
   itself as a magnitude dispatch candidate).  */

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

  /* Structural ceiling for this scan.  Regions beyond the second are
     only ever formable through the fp16 six-entry modes, so without
     that flag the scan keeps the historical two-region shape (and its
     refusal points) exactly.  */
  const unsigned max_pred
    = riscv_tt_opt_lut_select_fp16 ? RVTT_LUT_MAX_RANGES - 1 : 2;
  const unsigned budget = max_pred > 2 ? 160 : 64;

  unsigned region = 0;		/* current predicated region */
  unsigned closed = 0;		/* popcs consumed */

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
	case rvtt_insn_data::sfpxvif:
	  if (want != WANT_XVIF)
	    return *candidate
	      ? refuse ("lut-structure-mismatch", stmt) : false;
	  g->xvif[region] = stmt;
	  want = WANT_FCMP;
	  continue;

	case rvtt_insn_data::sfpxfcmps:
	  if (want != WANT_FCMP)
	    return *candidate
	      ? refuse ("lut-structure-mismatch", stmt) : false;
	  if (region == 0)
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
	    }
	  if (!match_lt_boundary (call, g->mag, &g->boundary_bits[region]))
	    return refuse ("lut-compare-kind-unsupported", stmt);
	  g->fcmp[region] = stmt;
	  want = WANT_CONDB;
	  continue;

	case rvtt_insn_data::sfpxcondb:
	  {
	    if (want != WANT_CONDB)
	      return *candidate
		? refuse ("lut-structure-mismatch", stmt) : false;
	    /* The condition tree must be exactly the compare, anchored
	       at this region's vif token.  */
	    tree c = gimple_call_arg (call, 0);
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
	  if (want != WANT_PUSHC || int_arg (call, 0) != 0)
	    return *candidate
	      ? refuse ("lut-partition-arity-unsupported", stmt) : false;
	  depth++;
	  region++;
	  g->pushc[region] = stmt;
	  want = WANT_XVIF;
	  continue;

	case rvtt_insn_data::sfppopc:
	  {
	    if (int_arg (call, 0) != 0
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
  /* Stage-A agreement with the CC-region tree:
     the region discipline this scan tracked with its own depth counter
     -- each else-arm frame nested under the previous, the popc chain
     draining innermost-first, each predicated assign inside its own
     frame, each frame's refinement chain exactly its
     xvif/fcmp/condb (+compc) words -- must be exactly what the shared
     frame analysis computed.  The scan is the compatibility predicate
     (the historical shape set); a disagreement is a FINDING (hard
     assert under flag_checking); release builds fail closed by name.
     The coefficient/leaf matching below is untouched (partial
     conversion by design).  */
  {
    bool tree_ok = true;
    rvtt_cc_region *prev = nullptr;
    for (unsigned r = 0; r < g->num_pred && tree_ok; r++)
      {
	rvtt_cc_region *reg = ccr->region_opened_by (g->pushc[r]);
	gimple *rpopc = g->popc[g->num_pred - 1 - r];
	unsigned want_refs = r + 1 < g->num_pred ? 4 : 3;
	tree_ok = reg && ccr->refinements_pure_p (reg)
	  && (r == 0 || reg->parent == prev)
	  && ccr->region_of (g->assign[r]) == reg
	  && reg->exits.length () == 1 && reg->exits[0] == rpopc
	  && reg->refinements.length () == want_refs
	  && reg->refinements[0] == g->xvif[r]
	  && reg->refinements[1] == g->fcmp[r]
	  && reg->refinements[2] == g->condb[r]
	  && (want_refs == 3 || reg->refinements[3] == g->compc[r]);
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
      /* Coefficient definitions may sit inside the region when every
	 use is a claimed leaf statement or another coefficient (all
	 pure): they simply outlive the tree as LUT operands.  */
      tree lhs = gimple_call_lhs (stmt);
      if (!lhs)
	return false;
      for (unsigned i = 0; i <= dflt; i++)
	if (lhs == g->a_coeff[i] || lhs == g->b_coeff[i])
	  return true;
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

  /* Trailing sign-restore fold candidate: the selected value's single
     non-debug consumer copies the LUT input's own sign onto it, with
     the vector sign-copy instruction in its default mode.  Purely
     structural -- the sign source must be exactly the LUT's input
     SSA value; any other consumer, operand order, source, or mode is
     not a fold candidate and keeps the explicit instruction.  */
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
      if (gcall *sgn = is_rvtt_call (use_stmt, rvtt_insn_data::sfpsetsgn_v))
	if (gimple_call_arg (sgn, 0) == g->result
	    && gimple_call_arg (sgn, 1) == g->x
	    && int_arg (sgn, 2) == 0
	    && gimple_call_lhs (sgn))
	  sgn_use = sgn;
  }

  /* Shape proven.  Now the capability check: a target mode must exist
     whose boundary set embeds these boundary encodings in ascending
     range order, with the required sign behavior and gates.  A fold
     candidate without a sign-restore capability falls back to the
     sign-update mode with the explicit sign copy kept.  Modes are
     tried in table order (rvtt-lut-tables.cc preference order), so a
     tree the FP32 3-entry table can host keeps forming exactly as it
     did before the six-entry rows existed.  */
  const rvtt_lut_mode_desc *table = nullptr;
  unsigned n_modes = rvtt_lut_modes (&table);
  if (!n_modes)
    return refuse ("lut-no-target-capability", g->pushc[0]);

  /* Wormhole fail-closed guard.  The exhaustive certification recorded
     in rvtt-lut-tables.cc found that on WH the tree-vs-LUT BUCKET
     agreement itself fails for the 8388607 negative-NaN inputs: the WH
     SFPABS keeps the -NaN sign and the WH compare-subtract inherits
     the operand's sign into its NaN result, so the source tree takes
     range 0 while the hardware LUT buckets |x| by magnitude into the
     top range (first witness input 0xff800001).  Every formation on WH
     therefore diverges from the tree it replaces on exactly those
     inputs.  The function's own -ffinite-math-only license excludes
     precisely that divergence set; without it, refuse by name.
     Blackhole's bucketing is certified (both arities) and is untouched
     here.  */
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

  /* Per-slot leaf admission against the certification table: each
     non-affine leaf class must be certified bit-exact for the slot's
     partition on this target under this function's finite-math
     license, and a constant leaf's value must belong to a certified
     value class.  Fail closed by name; a refusal edits nothing.  */
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

  /* Coefficient encoding admission.  The FP32 3-entry table holds any
     FP32 value verbatim; the LUT16-packed modes must derive every
     slot coefficient's compile-time value (the same audited
     materialization forms the constant-leaf derivation uses) and
     prove its exact re-encoding, then assemble the three packed
     A-words and three packed B-words.  */
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
      /* The synthesized +0.0 (and any value-known coefficient) must
	 re-encode exactly in the mode's coefficient encoding; the
	 FP32 3-entry table holds any FP32 value verbatim, so only a
	 non-affine leaf against a narrower future mode could trip
	 this (wired fail-closed).  */
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

  /* Slot operand assembly.  The LUT16-packed modes take the three
     packed A-words and three packed B-words as freshly synthesized
     invariant immediates; the FP32-direct modes take the leaves' own
     coefficient values, with synthesized +0.0 coefficients for the
     degenerate positions and own materializations for a duplicated
     leaf's later slots where the definition re-issues verbatim.  */
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
		dead_coeff_defs.safe_push (def);
	    }
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
  /* Under a packed mode the original coefficient materializations are
     dead once their leaves are gone (unless something else still uses
     them, in which case they stay).  A null lhs means the recursive
     single-use cleanup in the leaf deletions above already stripped
     the definition's result: the load is equally dead.  */
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

/* Place the formed LUT's in-loop coefficient materializations in the
   loop preheader.  The proofs are the invariant-loadi pass's shared
   discipline (rvtt-macro-ownership.h); the LREG-budget decision is
   transactional over the whole coefficient set, so a refusal keeps the
   bytes exactly at the formation-only shape.  A refusal never edits
   the program (rvtt_commit_hoist_preheader runs only after every proof
   has passed).  */

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
     possibly-zero-trip loop.  At this late stage counted loops are in
     rotated (test-at-the-latch) form, so the early pass's foldable
     header-test proof rarely applies; a load resident in the loop
     header needs no value proof at all -- every statement of the
     header except its terminating condition executes as soon as the
     loop is entered.  Both arguments are structural; each candidate
     must satisfy one.  */
  bool first_iteration = rvtt_loop_first_iteration_executes_p (loop, entry);

  /* The coefficient operands, deduplicated.  An operand already
     defined outside the loop is invariantly placed and needs no move.
     An in-loop definition qualifies for placement when it is an
     invariant immediate materialization that provably executes on
     each entered iteration; anything else (a constant-register read,
     derived arithmetic, or an unproven placement) simply stays where
     the program put it -- only the LREG budget below is transactional
     over the qualified set.  */
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
      /* Under the leaf extension, a read of a hardwired constant
	 register (LReg.md: LReg[9] zero, LReg[10] one; both read-only)
	 is as placeable as an invariant immediate: the value is
	 architectural, so the preheader read observes the same
	 constant every execution.  Mutable registers never qualify.  */
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

  /* Architectural LREG budget, transactional: either every coefficient
     stays live across the loop within the eight-LREG file or none
     moves.  For the FP16 packed modes (mod0 bit 1) the count exempts
     constant-register-file reads (LReg[8..14]): every operand position
     around the formed LUT accepts the creg class directly, so such a
     value never competes for the eight allocatable LREGs -- without the
     exemption a kernel-shaped row like gelu (six packed words + input +
     a PRGM-constant half) counts a phantom ninth LREG and forfeits the
     whole placement (measured on hardware: 71223 cycles -> the
     hand-shape loop).

     The FP32-direct path under the leaf extension gets the same
     exact-obligation counting: the leaf extension is what puts
     creg reads around the formed LUT in the first place (constant
     leaves read LReg[10], and the surrounding kernel arithmetic reads
     hardwired constants the earlier invariant hoist may have placed in
     the preheader), and without the exemption a preheader-hoisted
     hardwired-constant read counts a phantom LREG and forfeits the
     whole placement -- a measured tanhderivlut +47.5%% hardware residual
     was exactly this shape (an earlier build placed 6 coefficients; a
     later build's preheader hoist of the loop's `+ 1.0f' creg read
     flipped the count to 9 and every coefficient rematerialized per
     row).  The counting
     itself now refuses the exemption for a creg read feeding a LUT
     table slot (that copy is physical -- see creg_resident_p), so the
     budget stays an over-approximation of the file.  Without either
     flag the historical FP32-direct counting is byte-identical
     (default-off parameter).  */
  bool fp16_mode = (int_arg (lut, 7) & 2) != 0;
  bool creg_exempt = fp16_mode || riscv_tt_opt_lut_select_leaf_ext;
  if (!rvtt_pressure_loop_legal_p (loop, coeffs, /*report=*/true,
				   /*cc_transients=*/false,
				   /*exempt_creg_reads=*/creg_exempt))
    return keep ("lut-coefficient-pressure");

  basic_block preheader = rvtt_commit_hoist_preheader (entry);
  for (gcall *call : coeffs)
    {
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

static bool
transform (function *fun, auto_vec<gcall *> *formed)
{
  bool changed = false;
  basic_block bb;
  /* The CC-region tree, computed once per function
     (rvtt-cc-region.h).  A formation deletes whole frames and
     inserts only CC-inert statements, so the surviving frames' facts
     stay exact and no rebuild is needed between fires.  */
  rvtt_cc_region_tree ccr (fun);
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
	/* Coefficient placement, scoped to exactly the loops where a
	   LUT formed this execution: everything else keeps its
	   bytes.  */
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
    return changed ? TODO_update_ssa_only_virtuals | TODO_verify_all : 0;
  }
};

} // anonymous namespace

gimple_opt_pass *
make_pass_rvtt_lut_select (gcc::context *ctxt)
{
  return new pass_rvtt_lut_select (ctxt);
}
