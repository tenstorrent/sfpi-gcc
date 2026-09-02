/* Target capability tables for Tensix SFPU LUT instruction selection.
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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rvtt-lut-tables.h"

/* SFPLUTFP32 mode-word (Mod0) vocabulary, per the WH/BH SFPLUTFP32
   specification (tt-isa-documentation SFPLUTFP32.md; craq-sim
   tensix.cpp TENSIX_EXECUTE_SFPLUTFP32 decode, verified identical):
     value 0	     the FP32 three-entry table (LReg0-2 = A
		     coefficients, LReg4-6 = B coefficients, all
		     straight FP32; boundaries 1.0, 2.0)
     value 2	     FP16 six-entry TABLE1 (LReg0-2 = packed LUT16
		     A-coefficient pairs, LReg4-6 = packed LUT16
		     B-coefficient pairs; boundaries 0.5, 1.0, 1.5,
		     2.0, 3.0)
     value 3	     FP16 six-entry TABLE2 (as TABLE1 with the top
		     boundary at 4.0)
     +4		     SGN_RETAIN: copy the input's sign onto the result
		     (applied AFTER the slot MAD, copysign from the
		     original LReg[3])
     +8		     indirect destination via LReg7 (never emitted
		     here; the encoded-destination md pattern would be
		     wrong), and 10 = the FP16 3-entry table (whose
		     hardware write-redirect bug makes it unusable for
		     an encoded destination -- never emitted here)
   Both tables bucket on strict magnitude-bit compares of the input
   (|x| = sign-bit-cleared LReg[3]) and evaluate fma (A_i, |x|, B_i)
   with a single rounding; the sim decode proves mod values 2/3/6/7
   unambiguously select the six-entry tables (the FP16 3-entry
   selector requires bit 3).  */

#define SFPLUTFP32_MOD0_FP32_3ENTRY_TABLE 0
#define SFPLUTFP32_MOD0_FP16_6ENTRY_TABLE1 2
#define SFPLUTFP32_MOD0_FP16_6ENTRY_TABLE2 3
#define SFPLUTFP32_MOD0_SGN_UPDATE 0
#define SFPLUTFP32_MOD0_SGN_RETAIN 4

/* FP32 bit encodings of the architectural range boundaries.  */
#define RVTT_LUT_FP32_BITS_0P5 0x3f000000u
#define RVTT_LUT_FP32_BITS_1P0 0x3f800000u
#define RVTT_LUT_FP32_BITS_1P5 0x3fc00000u
#define RVTT_LUT_FP32_BITS_2P0 0x40000000u
#define RVTT_LUT_FP32_BITS_3P0 0x40400000u
#define RVTT_LUT_FP32_BITS_4P0 0x40800000u

/* Modes available on Wormhole and Blackhole, in preference order: the
   FP32 three-entry table first (no coefficient narrowing, arbitrary
   run-time coefficients -- a tree it can host keeps forming exactly
   as before the six-entry rows landed), then the FP16 six-entry
   TABLE1/TABLE2 modes (compile-time coefficients only, exact LUT16
   re-encode proofs, gated on -mtt-tensix-optimize-lut-select-fp16).
   Each table appears with either sign behavior -- SGN_UPDATE keeps
   the per-range MAD's sign, SGN_RETAIN copies the input operand's
   sign onto the result (the fold of a trailing explicit sign copy).
   Quasar deliberately has no table: its SFPLUT-family execution is
   unvalidated here, so the pass refuses.  */
static const rvtt_lut_mode_desc wh_bh_lut_modes[] =
{
  {
    "fp32-3entry-sgn-update",
    rvtt_insn_data::sfplutfp32_6r,
    SFPLUTFP32_MOD0_FP32_3ENTRY_TABLE | SFPLUTFP32_MOD0_SGN_UPDATE,
    3,
    { RVTT_LUT_FP32_BITS_1P0, RVTT_LUT_FP32_BITS_2P0, 0, 0, 0 },
    false,
    RVTT_LUT_COEFF_FP32_DIRECT,
    nullptr,
  },
  {
    "fp32-3entry-sgn-retain",
    rvtt_insn_data::sfplutfp32_6r,
    SFPLUTFP32_MOD0_FP32_3ENTRY_TABLE | SFPLUTFP32_MOD0_SGN_RETAIN,
    3,
    { RVTT_LUT_FP32_BITS_1P0, RVTT_LUT_FP32_BITS_2P0, 0, 0, 0 },
    true,
    RVTT_LUT_COEFF_FP32_DIRECT,
    nullptr,
  },
  {
    "fp16-6entry-t1-sgn-update",
    rvtt_insn_data::sfplutfp32_6r,
    SFPLUTFP32_MOD0_FP16_6ENTRY_TABLE1 | SFPLUTFP32_MOD0_SGN_UPDATE,
    6,
    { RVTT_LUT_FP32_BITS_0P5, RVTT_LUT_FP32_BITS_1P0,
      RVTT_LUT_FP32_BITS_1P5, RVTT_LUT_FP32_BITS_2P0,
      RVTT_LUT_FP32_BITS_3P0 },
    false,
    RVTT_LUT_COEFF_LUT16_PACKED,
    &riscv_tt_opt_lut_select_fp16,
  },
  {
    "fp16-6entry-t1-sgn-retain",
    rvtt_insn_data::sfplutfp32_6r,
    SFPLUTFP32_MOD0_FP16_6ENTRY_TABLE1 | SFPLUTFP32_MOD0_SGN_RETAIN,
    6,
    { RVTT_LUT_FP32_BITS_0P5, RVTT_LUT_FP32_BITS_1P0,
      RVTT_LUT_FP32_BITS_1P5, RVTT_LUT_FP32_BITS_2P0,
      RVTT_LUT_FP32_BITS_3P0 },
    true,
    RVTT_LUT_COEFF_LUT16_PACKED,
    &riscv_tt_opt_lut_select_fp16,
  },
  {
    "fp16-6entry-t2-sgn-update",
    rvtt_insn_data::sfplutfp32_6r,
    SFPLUTFP32_MOD0_FP16_6ENTRY_TABLE2 | SFPLUTFP32_MOD0_SGN_UPDATE,
    6,
    { RVTT_LUT_FP32_BITS_0P5, RVTT_LUT_FP32_BITS_1P0,
      RVTT_LUT_FP32_BITS_1P5, RVTT_LUT_FP32_BITS_2P0,
      RVTT_LUT_FP32_BITS_4P0 },
    false,
    RVTT_LUT_COEFF_LUT16_PACKED,
    &riscv_tt_opt_lut_select_fp16,
  },
  {
    "fp16-6entry-t2-sgn-retain",
    rvtt_insn_data::sfplutfp32_6r,
    SFPLUTFP32_MOD0_FP16_6ENTRY_TABLE2 | SFPLUTFP32_MOD0_SGN_RETAIN,
    6,
    { RVTT_LUT_FP32_BITS_0P5, RVTT_LUT_FP32_BITS_1P0,
      RVTT_LUT_FP32_BITS_1P5, RVTT_LUT_FP32_BITS_2P0,
      RVTT_LUT_FP32_BITS_4P0 },
    true,
    RVTT_LUT_COEFF_LUT16_PACKED,
    &riscv_tt_opt_lut_select_fp16,
  },
};

unsigned
rvtt_lut_modes (const rvtt_lut_mode_desc **modes)
{
  if (!TARGET_XTT_TENSIX || TARGET_XTT_TENSIX_QSR)
    return 0;
  *modes = wh_bh_lut_modes;
  return ARRAY_SIZE (wh_bh_lut_modes);
}

/* Extended leaf-class certification (laneCY, 2026-08-20).

   A LUT slot evaluates fma_model (A, |x|, B) -- one partially-fused
   multiply-add with a single rounding (tt-isa-documentation
   BlackholeA0 SFPMAD.md "IEEE754 conformance / divergence"; craq-sim
   @ 9f324140 src/fma.cpp fma_model_bh/_wh, src/tensix.cpp
   TENSIX_EXECUTE_SFPLUTFP32).  The facts below were certified by
   exhaustive enumeration against those pinned models; the sweep
   parameters and outcomes are recorded inline here, and any change to
   the models voids and re-owes the certification:

   - Multiply-only leaves (B := +0.0): SFPMAD.md states "adding zero
     [makes] the partially fused operation equivalent to a standalone
     multiply", and the SFPMUL executor is fma_model (a, b, +0.0)
     itself.  On Blackhole the identity fma (u, A, +0) ==
     fma (A, u, +0) held for the full bf16 x bf16 coefficient cross,
     for all 2^32 lane values against an adversarial coefficient set,
     and for every non-finite lane value against all 2^32 coefficients
     (including the SFPABS -NaN sign-keep quirk, SFPABS.md); the
     Blackhole model canonicalizes every NaN result to 0x7fc00000, so
     the operand-order and abs-vs-mask differences vanish.  Certified
     on BH for every partition without a finite-math license.

   - Constant leaves (A := +0.0): for every FINITE lane value the
     model's zero-product shortcut returns the addend's exact bits, so
     a slot (+0.0, C) reproduces the constant C bit-exactly over any
     BOUNDED partition -- certified for all 2^32 C values at
     representative partition points and for value-class
     representatives over entire partitions.  Over the TAIL (top,
     unbounded) partition the slot computes fma (+0.0, inf, C) = NaN
     and fma (+0.0, NaN, C) = NaN ("usual IEEE754 rules", SFPMAD.md),
     while the source tree's untouched default lanes keep C: the
     enumeration exhibits exactly 2^23 mismatching inputs (+inf and
     every NaN encoding; first witness input 0x7f800000).  A tail
     constant slot is therefore certified ONLY under the function's
     -ffinite-math-only license (the divergence set is precisely the
     inputs that license excludes); otherwise it refuses.

   - Value classes for C: the shortcut returns the addend VERBATIM
     only for normal values and +0.0.  -0.0 returns +0.0 (sign-and),
     denormal C flushes, and a NaN C canonicalizes (BH) or leaks
     payload bits (WH); those classes refuse.  (+/-inf and the BH
     canonical NaN also reproduce exactly, but are excluded here as
     useless-and-risky constants; extending the class needs only a new
     certification row.)

   - Wormhole: the tree-vs-slot BUCKET agreement itself fails on WH
     for the 8388607 negative-NaN inputs (SFPABS keeps the -NaN sign;
     the WH compare-subtract inherits the operand's sign into its NaN
     result, so the tree takes range 0 while the hardware LUT buckets
     by magnitude into the top range; first witness input 0xff800001),
     and WH NaN results carry non-canonical payloads (WH SFPMAD.md).
     No extension class is certified on WH without a finite-math
     license.  (The same enumeration shows the base all-affine
     formation shares this WH negative-NaN divergence; the pass now
     fails closed on it: gimple-rvtt-lut-select.cc refuses every WH
     formation without a -ffinite-math-only license by the name
     lut-wh-negative-nan-divergent.)

   Six-entry extension (certified the same way, against the same
   pinned models):

   - Bucket agreement for the six-range partitions: the tree compare
     chain (SFPABS float; per-boundary MAD-subtract; SETCC sign test)
     agrees with the hardware's magnitude-bit six-way bucketing
     (boundaries 0.5/1.0/1.5/2.0/{3.0,4.0}, sim decode) for ALL 2^32
     inputs on Blackhole, for both TABLE1 and TABLE2 -- NaN and
     infinity inputs land in the tail bucket on both paths, exactly
     as the certified three-entry partition does.  On Wormhole the
     same negative-NaN divergence as above appears (and only it), so
     WH stays behind the function's -ffinite-math-only license via
     the pass-wide guard.

   - Slot arithmetic: a six-entry slot evaluates the identical
     fma_model (A, |x|, B) with A and B decoded from their LUT16
     halves; the selector admits only coefficients whose FP32 value
     re-encodes EXACTLY (rvtt_lut16_encode_exact_p, checked per
     coefficient with the lut-coeff-encoding-unrepresentable refusal),
     so the slot computes bit-for-bit the same fma the source leaf's
     MAD computed, and the leaf-class facts above (mul0 zero-addend,
     constant zero-product, tail behavior at non-finite inputs) carry
     over unchanged -- re-verified by the laneGU enumeration over the
     full LUT16 coefficient grid at the non-finite input classes.  */

bool
rvtt_lut_leaf_class_certified_p (rvtt_lut_leaf_class cls,
				 bool tail_partition,
				 bool finite_math)
{
  if (!TARGET_XTT_TENSIX || TARGET_XTT_TENSIX_QSR)
    return false;

  /* Under -ffinite-math-only every recorded divergence set (inf/NaN
     lane inputs) is excluded by the user's own license; both targets'
     finite-input enumerations passed for both classes.  */
  if (finite_math)
    return true;

  /* Without the license: Blackhole only, and never a tail constant.  */
  if (!TARGET_XTT_TENSIX_BH)
    return false;
  switch (cls)
    {
    case RVTT_LUT_LEAF_MUL0:
      return true;
    case RVTT_LUT_LEAF_CONST:
      return !tail_partition;
    }
  return false;
}

bool
rvtt_lut_const_value_certified_p (uint32_t bits)
{
  uint32_t exp = (bits >> 23) & 255;
  if (bits == 0)
    return true;		/* +0.0 */
  return exp > 0 && exp < 255;	/* normal (any sign) */
}

/* Exact LUT16 re-encoding.  The decode this must invert is the
   architectural Lut16ToFp32 (SFPLUTFP32.md; craq-sim lut16_to_fp32):
     exp16 == 31  ->  +/-0.0
     otherwise    ->  (sign) (1 + man10/1024) * 2^(exp16 - 15)
   so the exactly representable FP32 values are +/-0.0 (canonical
   encoding sign<<15 | 0x7c00) and the normals with unbiased exponent
   in [-15, 15] whose low 13 mantissa bits are zero.  Infinities,
   NaNs, denormals, and -0.0's IEEE encoding all round-trip through
   the decode to DIFFERENT bits and are refused (fail closed).  */

bool
rvtt_lut16_encode_exact_p (uint32_t bits, uint16_t *enc)
{
  uint32_t sign = bits >> 31;
  uint32_t exp = (bits >> 23) & 255;
  uint32_t man = bits & 0x7fffff;

  if ((bits & 0x7fffffff) == 0)
    {
      /* +/-0.0 via the exponent-31 quirk; decode returns sign<<31
	 exactly.  */
      *enc = (uint16_t) ((sign << 15) | (31u << 10));
      return true;
    }
  if (exp == 0 || exp == 255)
    return false;		/* denormal, inf, NaN */
  /* Unbiased exponent must fit LUT16's [-15, 15] (encoded field
     0..30) and the mantissa must fit 10 bits.  */
  int uexp = (int) exp - 127;
  if (uexp < -15 || uexp > 15)
    return false;
  if (man & 0x1fff)
    return false;
  *enc = (uint16_t) ((sign << 15) | ((uint32_t) (uexp + 15) << 10)
		     | (man >> 13));
  return true;
}
