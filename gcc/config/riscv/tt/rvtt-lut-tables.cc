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
  },
  {
    "fp32-3entry-sgn-retain",
    rvtt_insn_data::sfplutfp32_6r,
    SFPLUTFP32_MOD0_FP32_3ENTRY_TABLE | SFPLUTFP32_MOD0_SGN_RETAIN,
    3,
    { RVTT_LUT_FP32_BITS_1P0, RVTT_LUT_FP32_BITS_2P0, 0 },
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
