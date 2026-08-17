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

/* One selectable LUT execution mode of a target's SFPLUT-family
   instruction.  These tables are the only home for the raw
   architectural facts the LUT instruction-selection pass consumes:
   which builtin implements the mode, the instruction mode word, the
   range-partition arity, and the exact range boundary encodings the
   hardware compares |x| against.  The pass itself matches only
   dataflow shape; everything value-like it checks comes from here.  */

struct rvtt_lut_mode_desc
{
  /* Human-readable mode name, used in dumps.  */
  const char *name;

  /* The emission builtin implementing this mode.  */
  rvtt_insn_data::insn_id insn;

  /* The instruction's mode-word (Mod0) value for this mode.  */
  unsigned mod0;

  /* Number of range partitions the mode evaluates (its select arity).
     A source dispatch tree must have exactly this many leaves.  */
  unsigned num_ranges;

  /* Ascending FP32 bit encodings of the num_ranges-1 range boundaries
     the hardware compares the magnitude |x| against (strict
     less-than, magnitude-bit compare; equivalent to an IEEE float
     compare for the non-negative magnitudes the pass proves).  */
  uint32_t boundary_bits[3];

  /* True when the mode replaces the result's sign with the sign of
     the input operand (SGN_RETAIN); false when the result keeps the
     sign the per-range MAD produced (SGN_UPDATE).  */
  bool sign_restore;
};

/* Return the descriptor for the requested range arity with the
   requested sign behavior on the current target, or null when the
   target has no such LUT capability (refusal).  */
extern const rvtt_lut_mode_desc *rvtt_lut_lookup (unsigned num_ranges,
						  bool sign_restore);

#endif /* GCC_RVTT_LUT_TABLES_H */
