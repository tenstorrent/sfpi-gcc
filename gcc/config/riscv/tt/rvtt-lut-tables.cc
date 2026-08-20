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
   specification:
     bit 1 (+bit 0)  FP16 entry-table selectors; both clear = the
		     FP32 three-entry table (LReg0-2 = A coefficients,
		     LReg4-6 = B coefficients, all straight FP32)
     bit 2	     SGN_RETAIN: copy the input's sign onto the result
     bit 3	     indirect destination via LReg7 (never emitted here;
		     the encoded-destination md pattern would be wrong)
   The FP32 three-entry table buckets on the input magnitude |x| with
   strict magnitude-bit compares against 1.0f and 2.0f and evaluates
   fma (A_i, |x|, B_i) with a single rounding.	*/

#define SFPLUTFP32_MOD0_FP32_3ENTRY_TABLE 0
#define SFPLUTFP32_MOD0_SGN_UPDATE 0
#define SFPLUTFP32_MOD0_SGN_RETAIN 4

/* FP32 bit encodings of the FP32 three-entry table's range boundaries.  */
#define RVTT_LUT_FP32_BITS_1P0 0x3f800000u
#define RVTT_LUT_FP32_BITS_2P0 0x40000000u

/* Modes available on Wormhole and Blackhole: the FP32 three-entry
   table, whose coefficients need no packing or narrowing (any FP32
   value is encodable), with either sign behavior -- SGN_UPDATE keeps
   the per-range MAD's sign, SGN_RETAIN copies the input operand's sign
   onto the result (the second increment's fold of a trailing explicit
   sign copy).  Quasar deliberately has no table: its SFPLUT-family
   execution is unvalidated here, so the pass refuses.  */
static const rvtt_lut_mode_desc wh_bh_lut_modes[] =
{
  {
    "fp32-3entry-sgn-update",
    rvtt_insn_data::sfplutfp32_6r,
    SFPLUTFP32_MOD0_FP32_3ENTRY_TABLE | SFPLUTFP32_MOD0_SGN_UPDATE,
    3,
    { RVTT_LUT_FP32_BITS_1P0, RVTT_LUT_FP32_BITS_2P0, 0 },
    false,
    true,
  },
  {
    "fp32-3entry-sgn-retain",
    rvtt_insn_data::sfplutfp32_6r,
    SFPLUTFP32_MOD0_FP32_3ENTRY_TABLE | SFPLUTFP32_MOD0_SGN_RETAIN,
    3,
    { RVTT_LUT_FP32_BITS_1P0, RVTT_LUT_FP32_BITS_2P0, 0 },
    true,
    true,
  },
};

const rvtt_lut_mode_desc *
rvtt_lut_lookup (unsigned num_ranges, bool sign_restore)
{
  if (!TARGET_XTT_TENSIX || TARGET_XTT_TENSIX_QSR)
    return nullptr;

  for (const auto &mode : wh_bh_lut_modes)
    if (mode.num_ranges == num_ranges && mode.sign_restore == sign_restore)
      return &mode;

  return nullptr;
}

/* Extended leaf-class certification (laneCY, 2026-08-20).

   A LUT slot evaluates fma_model (A, |x|, B) -- one partially-fused
   multiply-add with a single rounding (tt-isa-documentation
   BlackholeA0 SFPMAD.md "IEEE754 conformance / divergence"; craq-sim
   @ 9f324140 src/fma.cpp fma_model_bh/_wh, src/tensix.cpp
   TENSIX_EXECUTE_SFPLUTFP32).  The facts below were certified by
   exhaustive enumeration against those pinned models (certifier and
   raw logs archived in laneCY-evidence-20260820/admission-proofs/):

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
     formation shares this WH negative-NaN divergence; that
     pre-existing finding is reported in the lane evidence, not
     changed here.)  */

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
