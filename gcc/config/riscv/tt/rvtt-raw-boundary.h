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

/* Canonical-word EXTRACTION only (the TTI_ macro shape: an
   output-and-clobber-free `.ttinsn %0' asm with one constant input):
   fills *WORD and returns true; anything else refuses.  No
   classification happens here -- every caller must apply its own
   audited, refusing-default classification to the word.  */
extern bool rvtt_raw_ttinsn_word (rtx_insn *insn, uint32_t *word);

/* Architectural replay-owner opcode test on an extracted word (field
   derivation against the target encoding table; unproven targets answer
   true, the refusing direction).  */
extern bool rvtt_raw_replay_owner_word_p (uint32_t word);

/* Audited CC/lane-enable classification of one raw instruction word
   (lane IV, the typecast walk-transparency class).  The question is the
   entry-ambient walk's: can this word disturb the architectural
   all-lanes lane-enable state (the per-lane LaneFlags /
   UseLaneFlagsForLaneEnable pair written by SFPENCC and friends, plus
   the LaneConfig ROW_MASK lane-predication bits)?

     RVTT_RAW_CC_INERT       -- the word's every architectural arm is
                                proven to leave lane-enable state
                                untouched (audited opcode classes below);
     RVTT_RAW_CC_ALL_LANES   -- the word writes lane-enable state, and
                                the written value is provably the
                                all-lanes ambient (the word-exact
                                canonical SFPENCC, or the audited
                                LaneConfig default-reset);
     RVTT_RAW_CC_UNPROVEN    -- everything else (fail-closed: unaudited
                                opcodes, expander words whose delivered
                                content this word does not carry,
                                lane-enable writers of unproven value).

   Consumers must treat both proven classes as AMBIENT-PRESERVING only
   (a state already all-lanes stays all-lanes) and never as a KILL: a
   raw word can sit inside a REPLAY record load window, where it is
   architecturally swallowed (stored, not executed -- lane HS,
   rvtt-mop-tables.h), so its execution can never be asserted from the
   word alone.  Preserving-classification is sound under both readings;
   kill-classification is not.  QSR has no capability table and every
   word answers UNPROVEN.  */

enum rvtt_raw_cc_class
{
  RVTT_RAW_CC_UNPROVEN = 0,
  RVTT_RAW_CC_INERT,
  RVTT_RAW_CC_ALL_LANES
};

extern rvtt_raw_cc_class rvtt_raw_cc_word_class (uint32_t word);

/* Convenience: the word provably cannot take the lane-enable state away
   from the all-lanes ambient (either proven class above).  */
extern bool rvtt_raw_cc_word_ambient_preserving_p (uint32_t word);

#endif /* GCC_RVTT_RAW_BOUNDARY_H */
