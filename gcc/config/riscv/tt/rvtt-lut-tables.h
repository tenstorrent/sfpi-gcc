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

#ifndef GCC_RVTT_LUT_TABLES_H
#define GCC_RVTT_LUT_TABLES_H

#include "rvtt.h"

/* How a mode's coefficient slots encode their values.  */
enum rvtt_lut_coeff_encoding
{
  /* Each slot holds any FP32 value verbatim in its own LReg.  */
  RVTT_LUT_COEFF_FP32_DIRECT,

  /* Two adjacent slots pack into one LReg as a pair of LUT16 halves
     (low half = even slot, high half = odd slot).  LUT16 is the
     SFPLUTFP32 coefficient format (tt-isa-documentation SFPLUTFP32.md
     Lut16ToFp32; the reference simulator's lut16_to_fp32): NOT IEEE FP16 --
     a 5-bit exponent field of 31 decodes to +/-0.0, every other
     exponent (0 included) decodes as a normal with implicit leading
     one, (1 + man/1024) * 2^(exp-15).  Every compile-time coefficient
     must prove exact re-encoding (rvtt_lut16_encode_exact_p) or
     refuse lut-coeff-encoding-unrepresentable.  */
  RVTT_LUT_COEFF_LUT16_PACKED,
};

/* One selectable LUT execution mode of a target's SFPLUT-family
   instruction.  These tables are the only home for the raw
   architectural facts the LUT instruction-selection pass consumes:
   which builtin implements the mode, the instruction mode word, the
   range-partition arity, the exact range boundary encodings the
   hardware buckets |x| with, and the coefficient encoding.  The pass
   itself matches only dataflow shape; everything value-like it checks
   comes from here.  */

struct rvtt_lut_mode_desc
{
  /* Human-readable mode name, used in dumps.  */
  const char *name;

  /* The emission builtin implementing this mode.  */
  rvtt_insn_data::insn_id insn;

  /* The instruction's mode-word (Mod0) value for this mode.  */
  unsigned mod0;

  /* Number of range partitions the mode evaluates (its select arity).
     A source dispatch tree must have at most this many leaves; fewer
     leaves form by slot duplication under the leaf extension.  */
  unsigned num_ranges;

  /* Ascending FP32 bit encodings of the num_ranges-1 range boundaries
     the hardware compares the magnitude |x| against (strict
     less-than, magnitude-bit compare; equivalent to an IEEE float
     compare for the non-negative magnitudes the pass proves).  */
  uint32_t boundary_bits[5];

  /* True when the mode replaces the result's sign with the sign of
     the input operand (SGN_RETAIN); false when the result keeps the
     sign the per-range MAD produced (SGN_UPDATE).  */
  bool sign_restore;

  /* The mode's coefficient encoding.  RVTT_LUT_COEFF_FP32_DIRECT
     holds arbitrary run-time FP32 coefficient values;
     RVTT_LUT_COEFF_LUT16_PACKED requires every coefficient to be a
     compile-time-provable constant whose value re-encodes exactly
     (lut-coeff-value-unproven / lut-coeff-encoding-unrepresentable
     refusals otherwise).  */
  rvtt_lut_coeff_encoding coeff_encoding;

  /* Optimization flag gating this mode's availability to the
     selector, or null when the base lut-select flag suffices.  The
     flag variables are int; the mode is available when *gate > 0.  */
  const int *gate;
};

/* Return the target's LUT mode table through *MODES (table order is
   preference order) and the number of rows, or 0 when the target has
   no LUT capability at all.  */
extern unsigned rvtt_lut_modes (const rvtt_lut_mode_desc **modes);

/* Leaf classes the extended selector can place into a LUT slot beyond
   the plain affine leaf: a multiply-only leaf (the slot's B coefficient
   is a synthesized +0.0) and a compile-time-provable constant leaf (the
   slot's A coefficient is a synthesized +0.0).  */
enum rvtt_lut_leaf_class
{
  RVTT_LUT_LEAF_MUL0,
  RVTT_LUT_LEAF_CONST,
};

/* True when CLS is certified bit-exact against the LUT slot semantics
   on the current target for a slot whose input partition is
   TAIL_PARTITION (the top, unbounded range) or not, given the
   function's finite-math license.  The certification facts are
   recorded per-target in rvtt-lut-tables.cc with their exhaustive
   enumeration provenance; anything not recorded there is false
   (refusal).  */
extern bool rvtt_lut_leaf_class_certified_p (rvtt_lut_leaf_class cls,
					     bool tail_partition,
					     bool finite_math);

/* True when the compile-time constant-leaf value BITS belongs to a
   value class whose bit-exact slot reproduction is certified (see
   rvtt-lut-tables.cc); anything else refuses.  */
extern bool rvtt_lut_const_value_certified_p (uint32_t bits);

/* True when the FP32 value BITS re-encodes EXACTLY in the LUT16
   coefficient format (decode (encode (BITS)) == BITS); the canonical
   encoding is returned through *ENC.  +/-0.0 encode via the
   exponent-31 quirk (0x7c00 / 0xfc00); representable magnitudes are
   the 10-bit-mantissa normals with unbiased exponent in [-15, 15].
   Everything else (infinities, NaNs, denormals, wider mantissas or
   exponents) returns false.  */
extern bool rvtt_lut16_encode_exact_p (uint32_t bits, uint16_t *enc);

#endif /* GCC_RVTT_LUT_TABLES_H */
