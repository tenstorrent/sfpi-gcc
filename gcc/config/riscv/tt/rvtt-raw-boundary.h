/* Audited architectural decode of raw `.ttinsn' boundary words.
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

#ifndef GCC_RVTT_RAW_BOUNDARY_H
#define GCC_RVTT_RAW_BOUNDARY_H

#include "rvtt-effects.h"

/* The LLK library issues some architectural boundary instructions as
   raw constant `.ttinsn' words (the TTI_ macro shape of ckernel_ops.h)
   rather than through typed builtins.  The upstream-pristine rule
   forbids replacing them with typed wrappers, so the late analyses must
   DERIVE what such a word does or refuse.

   This is the one RTL-side decoder of that shape.  Discipline (the same
   one rvtt-macro-epoch.cc established for its config-epoch question):
   the canonical single-constant `.ttinsn %0' asm is field-decoded
   against the capability-table encoding facts and classified by
   ARCHITECTURAL opcode/field class -- never by operation identity or
   whole-word matching.  Exactly one class is on record:

     pure Dst/RWC counter write -- a SETRWC-class word that writes
     nothing but the Dst RWC counter pair (no SrcA/SrcB counter or
     bank-valid effect, no fidelity-phase reset, no LREG, CC, Dst-memory,
     or configuration effect).  This is the run/row-separator class of
     the macro-planner vocabulary (the class the typed rvtt_ttsetrwc /
     rvtt_ttdstface patterns carry); the derived effect set mirrors the
     typed TTSETRWC effect set exactly.

   Every other word -- another opcode class (SFPCONFIG, SETC16, loads,
   stores, ...), a Src-leg or bank-valid SETRWC, a fidelity reset, a
   non-constant operand, a non-canonical template -- keeps the refusing
   opaque default, so consumers refuse byte-identically, by their
   existing names.

   Returns true and fills *RWC (kind SET, the Dst-only set_mask) when
   INSN is a proven pure Dst/RWC raw word.  QSR has no capability table
   and always refuses.  */

extern bool rvtt_raw_pure_dst_rwc (rtx_insn *insn, xtt_rwc_effect_t *rwc);

/* The same audited classification at the gimple level: a GIMPLE_ASM
   whose canonical single-constant `.ttinsn %0' word decodes to the pure
   Dst/RWC counter class.  Same refusing defaults as the RTL entry
   point.  */
extern bool rvtt_raw_pure_dst_rwc_gimple (const gimple *stmt);

#endif /* GCC_RVTT_RAW_BOUNDARY_H */
