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
   specification (SFPLUTFP32 in the Tensix ISA documentation):
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
		     (applied after the slot MAD, copysign from the
		     original LReg[3])
     +8		     indirect destination via LReg7 (never emitted
		     here; the encoded-destination md pattern would be
		     wrong), and 10 = the FP16 3-entry table (whose
		     hardware write-redirect bug makes it unusable for
		     an encoded destination -- never emitted here)
   Both tables bucket on strict magnitude-bit compares of the input
   (|x| = sign-bit-cleared LReg[3]) and evaluate fma (A_i, |x|, B_i)
   with a single rounding; mod values 2/3/6/7 unambiguously select
   the six-entry tables (the FP16 3-entry selector requires bit
   3).  */

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

/* Modes available on Wormhole and Blackhole, in preference order: the FP32
   three-entry table first (no coefficient narrowing, arbitrary run-time
   coefficients, so a tree it can host never needs the narrower encodings),
   then the FP16 six-entry TABLE1/TABLE2 modes (compile-time coefficients
   that re-encode exactly into LUT16, enabled by
   -mtt-tensix-optimize-lut-select-fp16).  Each table appears with either
   sign behavior -- SGN_UPDATE keeps the per-range MAD's sign, SGN_RETAIN
   copies the input operand's sign onto the result (the fold of a trailing
   explicit sign copy).  Quasar has no table because its SFPLUT-family
   execution has not been validated.  */
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

/* Return the number of LUT modes the current target supports and set *MODES
   to the descriptor table (in selection-preference order).  Returns 0 --
   LUT formation refuses -- on non-Tensix targets and on Quasar, whose
   SFPLUT-family execution has not been validated.  */

unsigned
rvtt_lut_modes (const rvtt_lut_mode_desc **modes)
{
  if (!TARGET_XTT_TENSIX || TARGET_XTT_TENSIX_QSR)
    return 0;
  *modes = wh_bh_lut_modes;
  return ARRAY_SIZE (wh_bh_lut_modes);
}

/* Return true if a LUT slot can replace a leaf of class CLS without
   changing results.  TAIL_PARTITION says whether the leaf covers the top,
   unbounded range; FINITE_MATH whether -ffinite-math-only is in effect.

   A LUT slot evaluates fma (A, |x|, B) with a single rounding, like SFPMAD
   (see "IEEE754 conformance" under SFPMAD in the Blackhole ISA
   documentation).  The following facts were established by exhaustive
   enumeration against models of SFPMAD and SFPLUTFP32:

   - Multiply-only leaves (B = +0.0): adding zero makes SFPMAD equivalent
     to a standalone multiply, and on Blackhole fma (u, A, +0) equals
     fma (A, u, +0) for all inputs, including non-finite ones, because
     Blackhole canonicalizes every NaN result to 0x7fc00000.

   - Constant leaves (A = +0.0): for every finite input the zero product
     returns the addend's exact bits, so a slot (+0.0, C) reproduces C over
     any bounded range.  In the top range, however, +inf and NaN inputs
     give NaN where the source keeps C, so a tail constant is only valid
     under -ffinite-math-only.

   - On Wormhole, the compare chain and the hardware's magnitude bucketing
     disagree for negative NaN inputs (SFPABS keeps the sign of -NaN), and
     NaN results carry non-canonical payloads, so nothing is accepted
     there without -ffinite-math-only.  gimple-rvtt-lut-select.cc refuses
     all Wormhole formation without it for the same reason.

   The six-entry tables bucket identically to the source compare chain for
   all inputs on Blackhole (NaN and infinity land in the top bucket on both
   paths), and since only coefficients that re-encode exactly into LUT16
   are accepted (rvtt_lut16_encode_exact_p), each slot computes the same
   fma as the source leaf; the facts above carry over unchanged.  */

bool
rvtt_lut_leaf_class_certified_p (rvtt_lut_leaf_class cls,
				 bool tail_partition,
				 bool finite_math)
{
  if (!TARGET_XTT_TENSIX || TARGET_XTT_TENSIX_QSR)
    return false;

  /* Every known divergence involves infinite or NaN inputs.  */
  if (finite_math)
    return true;

  /* Without -ffinite-math-only: Blackhole only, and never a tail
     constant.  */
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

/* Return true if the FP32 value BITS can be a constant leaf: +0.0 or a
   normal value of either sign, which the zero-product path returns
   unchanged.  -0.0 becomes +0.0, denormals are flushed and NaNs are
   canonicalized (Blackhole) or keep payload bits (Wormhole), so those are
   refused, as are infinities.  */

bool
rvtt_lut_const_value_certified_p (uint32_t bits)
{
  uint32_t exp = (bits >> 23) & 255;
  if (bits == 0)
    return true;		/* +0.0 */
  return exp > 0 && exp < 255;	/* normal (any sign) */
}

/* If the FP32 value BITS has an exact LUT16 encoding, store it in *ENC
   and return true.  The hardware decode (Lut16ToFp32, see SFPLUTFP32 in
   the Tensix ISA documentation) is
     exp16 == 31  ->  +/-0.0
     otherwise    ->  (sign) (1 + man10/1024) * 2^(exp16 - 15)
   so the representable values are +/-0.0 (encoded as sign<<15 | 0x7c00)
   and normals with unbiased exponent in [-15, 15] whose low 13 mantissa
   bits are zero.  */

bool
rvtt_lut16_encode_exact_p (uint32_t bits, uint16_t *enc)
{
  uint32_t sign = bits >> 31;
  uint32_t exp = (bits >> 23) & 255;
  uint32_t man = bits & 0x7fffff;

  if ((bits & 0x7fffffff) == 0)
    {
      /* +/-0.0 via the exponent-31 encoding.  */
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
