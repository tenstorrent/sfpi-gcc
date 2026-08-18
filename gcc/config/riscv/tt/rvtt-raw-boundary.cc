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

#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "rtl.h"
#include "insn-config.h"
#include "recog.h"
#include "tm_p.h"
#include "rvtt-effects.h"
#include "rvtt-macro-tables.h"
#include "rvtt-raw-boundary.h"

/* Architectural purity of one decoded word, field by field (semantics
   documented at setrwc_decode; concordant ISA spec + simulator
   executor):

   - clear_ab_vld clears SrcA/SrcB bank data-valid -- a cross-thread
     bank handshake effect, never pure;
   - bit_mask must select exactly the Dst leg (bit 2): the SrcA/SrcB
     bits write foreign counters and the fidelity bit resets the FPU
     fidelity phase; the two undocumented high mask bits refuse;
   - rwc_cr's SrcA/SrcB CR-mode bits pair with the corresponding mask
     bits, so they must be clear as well.  Its Dst bits (CR-relative,
     current-relative) and the absolute form are all pure Dst counter
     writes and all admitted;
   - the rwc_a/rwc_b value fields are dead under a Dst-only mask, but
     an audited class pins them at zero rather than trusting
     dead-field behavior.

   The capability table is the per-target admission: QSR (and any
   future CPU without a table) refuses everything.  */

static bool
pure_dst_rwc_word_p (uint32_t word)
{
  rvtt_macro::cpu_t cpu = TARGET_XTT_TENSIX_BH ? rvtt_macro::CPU_BH
    : TARGET_XTT_TENSIX_WH ? rvtt_macro::CPU_WH : rvtt_macro::CPU_QSR;
  if (!rvtt_macro_caps_for_cpu (cpu))
    return false;

  rvtt_macro::setrwc_fields f;
  if (!rvtt_macro::setrwc_decode (word, &f))
    return false;

  if (f.clear_ab_vld != 0)
    return false;
  if (f.bit_mask != 0x4)
    return false;
  if ((f.rwc_cr & 0x3) != 0)
    return false;
  if (f.rwc_a != 0 || f.rwc_b != 0)
    return false;
  return true;
}

/* Canonical `.ttinsn %0' template check, shared by both extractors.  */

static bool
canonical_ttinsn_template_p (const char *s)
{
  if (!s)
    return false;
  while (*s == ' ' || *s == '\t')
    ++s;
  if (strncmp (s, ".ttinsn", 7) != 0)
    return false;
  s += 7;
  while (*s == ' ' || *s == '\t')
    ++s;
  return strcmp (s, "%0") == 0;
}

/* Extract the constant instruction word of the canonical raw form: an
   operand-less-output `.ttinsn %0' asm with exactly one constant input
   (the TTI_ macro shape of ckernel_ops.h INSTRUCTION_WORD).  Anything
   else -- outputs, several inputs or words, a register operand, a
   different template -- refuses.  This mirrors the template discipline
   of rvtt-macro-epoch.cc's epoch_asm_check (unification of the two
   extractors is a noted follow-up; behavior must stay refusing-default
   in both).  */

static bool
raw_ttinsn_const_word (rtx_insn *insn, uint32_t *word)
{
  if (!insn || !NONDEBUG_INSN_P (insn) || CALL_P (insn) || JUMP_P (insn))
    return false;
  rtx pat = PATTERN (insn);
  /* Only the bare ASM_OPERANDS form: no outputs (a SET or PARALLEL
     around the asm means outputs or clobbers -- not the canonical
     TTI_ shape).  */
  if (GET_CODE (pat) != ASM_OPERANDS)
    return false;
  if (!canonical_ttinsn_template_p (ASM_OPERANDS_TEMPLATE (pat)))
    return false;
  if (ASM_OPERANDS_INPUT_LENGTH (pat) != 1
      || ASM_OPERANDS_LABEL_LENGTH (pat) != 0)
    return false;
  rtx input = ASM_OPERANDS_INPUT (pat, 0);
  if (!CONST_INT_P (input))
    return false;
  uint64_t value = UINTVAL (input) & 0xffffffffu;
  *word = (uint32_t) value;
  return true;
}

/* See rvtt-raw-boundary.h: public canonical-word extraction (no
   classification -- callers apply their own audited, refusing-default
   classification to the extracted word).  */

bool
rvtt_raw_ttinsn_word (rtx_insn *insn, uint32_t *word)
{
  return raw_ttinsn_const_word (insn, word);
}

/* See rvtt-raw-boundary.h: is WORD an architectural replay-owner word?
   Field derivation only -- the opcode byte is compared against the
   REPLAY encoding of the target's encoding table (the same source the
   typed TTREPLAY emission uses), never against a whole-word value.
   Unproven targets answer true (the refusing direction for every
   caller: an owner word inside a capture refuses).  */

bool
rvtt_raw_replay_owner_word_p (uint32_t word)
{
  unsigned opcode = word >> 24;
  if (TARGET_XTT_TENSIX_BH)
    return opcode == (TT_OP_BH_REPLAY (0, 0, 0, 0) >> 24);
  if (TARGET_XTT_TENSIX_WH)
    return opcode == (TT_OP_WH_REPLAY (0, 0, 0, 0) >> 24);
  return true;
}

/* See rvtt-raw-boundary.h.  */

bool
rvtt_raw_pure_dst_rwc (rtx_insn *insn, xtt_rwc_effect_t *rwc)
{
  uint32_t word;
  if (!raw_ttinsn_const_word (insn, &word))
    return false;
  if (!pure_dst_rwc_word_p (word))
    return false;

  /* The derived effect mirrors the typed TTSETRWC vocabulary entry:
     a SET-class Dst/RWC counter effect over the Dst leg.  (Kind SET,
     not INC: SETRWC is architecturally the set-class instruction even
     in its CR-relative modes, and consumers treat SET exactly as they
     treated the typed face advance -- a run separator, never a row's
     own absorbable increment.)  */
  rwc->kind = xtt_rwc_effect_t::SET;
  rwc->dst_delta = 0;
  rwc->cr_delta = 0;
  rwc->set_mask = 0x4;
  return true;
}

/* See rvtt-raw-boundary.h.  */

bool
rvtt_raw_pure_dst_rwc_gimple (const gimple *stmt)
{
  const gasm *g = dyn_cast <const gasm *> (stmt);
  if (!g)
    return false;
  /* Canonical TTI_ shape only: one constant input, no outputs, no
     clobbers, no labels.  */
  if (gimple_asm_noutputs (g) != 0
      || gimple_asm_ninputs (g) != 1
      || gimple_asm_nclobbers (g) != 0
      || gimple_asm_nlabels (g) != 0)
    return false;
  if (!canonical_ttinsn_template_p (gimple_asm_string (g)))
    return false;
  tree input = TREE_VALUE (gimple_asm_input_op (g, 0));
  if (TREE_CODE (input) != INTEGER_CST || !tree_fits_uhwi_p (input))
    return false;
  uint64_t value = tree_to_uhwi (input);
  if (value > 0xffffffffu)
    return false;
  return pure_dst_rwc_word_p ((uint32_t) value);
}
