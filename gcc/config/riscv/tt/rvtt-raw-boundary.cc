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

/* ------------------------------------------------------------------ */
/* Lane IV: audited CC/lane-enable word classification.

   Discipline identical to the pure-Dst/RWC class above and to
   rvtt_mop_audited_word_p (rvtt-mop-derive.cc): every admitted class is
   a recorded architectural fact with provenance, classified by
   opcode/field derivation -- never by operation identity or trust in
   the emitting library.  A word not covered by a recorded fact answers
   UNPROVEN (the refusing direction for every consumer).

   The state under audit is the Vector Unit's lane-enable predication:
   the per-lane LaneFlags / UseLaneFlagsForLaneEnable pair (SFPENCC.md
   functional model) and the LaneConfig ROW_MASK predication bits
   (SFPCONFIG.md LaneConfig table).  Provenance keys:

     [ISA]  tt-isa-documentation BlackholeA0+WormholeB0
            TensixTile/TensixCoprocessor (the mandatory prior); the
            named .md file carries the cited functional model.
     [SIM]  craq-sim src/tensix.cpp (pinned tree) executors.
     [MOPT] an audited fact already recorded in rvtt-mop-tables.h /
            rvtt_mop_audited_word_p with its own provenance; the CC
            question is implied by the recorded effect confinement.  */

rvtt_raw_cc_class
rvtt_raw_cc_word_class (uint32_t word)
{
  rvtt_macro::cpu_t cpu = TARGET_XTT_TENSIX_BH ? rvtt_macro::CPU_BH
    : TARGET_XTT_TENSIX_WH ? rvtt_macro::CPU_WH : rvtt_macro::CPU_QSR;
  if (!rvtt_macro_caps_for_cpu (cpu))
    return RVTT_RAW_CC_UNPROVEN;

  unsigned opcode = word >> 24;

  /* Tensix NOPs.  0x00 executes nothing; 0x02 is swallowed at the
     instruction FIFO and delivers nothing.  [MOPT] NOP facts.  */
  if (opcode == 0x00 || opcode == 0x02)
    return RVTT_RAW_CC_INERT;

  /* Sync family (ATGETM/ATRELM/SEMINIT/SEMPOST/SEMGET/SEMWAIT/
     STALLWAIT/...): semaphore, mutex
     and stall plumbing -- no SFPU lane state in any functional model.
     [MOPT] sync-family fact; [ISA] SEMWAIT.md/STALLWAIT arms.  */
  if (opcode >= 0xA0 && opcode <= 0xA7)
    return RVTT_RAW_CC_INERT;

  /* Thread-config family (SETC16 and neighbours): writes the backend
     thread configuration space, disjoint from the Vector Unit's lane
     state.  [MOPT] thread-config fact; [ISA] SETC16.md.  */
  if (opcode >= 0xB0 && opcode <= 0xB8)
    return RVTT_RAW_CC_INERT;

  /* CLEARDVALID / SETRWC: Src/Dst counter and bank-valid bookkeeping
     only.  [MOPT]; [ISA] SETRWC.md.  */
  if (opcode == 0x36 || opcode == 0x37)
    return RVTT_RAW_CC_INERT;

  /* ELWADD / MOVA2D: matrix-unit data path (Src banks, Dst rows, RWC);
     l_regs and lane predication untouched for every field value.
     [MOPT] (spec + simulator citations recorded there).  */
  if (opcode == 0x28 || opcode == 0x12)
    return RVTT_RAW_CC_INERT;

  /* SFPLOADI: writes LReg[VD] (or, VD >= 12 with backdoor loads
     enabled, the LoadMacroConfig instruction template) -- never lane
     flags, in either arm.  The dest >= 8 PRGM concern of the mop audit
     is a PRGM-file question, not a lane-enable one.  [ISA] SFPLOADI.md;
     SFPCONFIG.md DISABLE_BACKDOOR_LOAD row.  */
  if (opcode == 0x71)
    return RVTT_RAW_CC_INERT;

  /* SFPCONFIG: every VD != 15 arm writes LoadMacroConfig storage or
     LReg[11..14]; only the VD == 15 arm writes LaneConfig (which holds
     the ROW_MASK predication bits).  [ISA] SFPCONFIG.md functional
     model (no arm writes LaneFlags/UseLaneFlagsForLaneEnable).  The
     admitted VD == 15 class is the audited default-reset word
     (MOD1_IMM16_IS_VALUE set, imm16 == 0 -- TTI_SFPCONFIG (0, 0xF, 1),
     0x910000F1): the resulting LaneConfig is the default all-lanes,
     ROW_MASK == 0 state for every mod1 completion ([MOPT] LaneConfig
     default-reset fact, lane AR audit) -- an ambient-establishing
     write.  Any other VD == 15 word can set ROW_MASK and refuses.  */
  if (opcode == 0x91)
    {
      unsigned dest = (word >> 4) & 0xf;
      if (dest != 15)
	return RVTT_RAW_CC_INERT;
      if ((word & 1) == 1 && ((word >> 8) & 0xffff) == 0)
	return RVTT_RAW_CC_ALL_LANES;
      return RVTT_RAW_CC_UNPROVEN;
    }

  /* SFPCAST / SFPSTOCHRND: every mode of both functional models writes
     only LReg[VD] (and only when VD < 8 || VD == 16); with VD >= 12 and
     backdoor loads enabled the word is instead captured into
     LoadMacroConfig.InstructionTemplate[VD-12] and executes nothing --
     the LLK load-macro template-programming idiom (the typecast init's
     0x900000C0 / 0x8E0000D1 words).  Lane-enable state is untouched
     under BOTH the executed and the captured reading, so the class
     needs no DISABLE_BACKDOOR_LOAD knowledge.  [ISA]
     SFPCAST_{IntFloat,IntInt,IntAbs}.md,
     SFPSTOCHRND_{FloatFloat,FloatInt,IntInt}.md, SFPCONFIG.md
     DISABLE_BACKDOOR_LOAD row; [SIM] tensix.cpp SFPU dispatch
     lreg_dest 12..15 capture arm + TENSIX_EXECUTE_SFPCAST/
     SFP_STOCH_RND.  */
  if (opcode == 0x90 || opcode == 0x8E)
    return RVTT_RAW_CC_INERT;

  /* SFPENCC: the lane-enable writer itself.  Only the word-exact
     canonical all-lanes encoding is proven (the same capability-table
     word the typed kill test and the synthesized enable stand on:
     rvtt_macro::sfpencc_all_lanes_word, imm12 == SFPENCC_IMM12_BOTH,
     mod1 == SFPENCC_MOD1_EI_RI, VD == 0).  Every other field
     combination -- lanes-off, complement, VD >= 12 capture forms --
     refuses.  [ISA] SFPENCC.md; [SIM] TENSIX_EXECUTE_SFPENCC.  */
  if (opcode == 0x8A)
    {
      if (word == rvtt_macro::sfpencc_all_lanes_word ())
	return RVTT_RAW_CC_ALL_LANES;
      return RVTT_RAW_CC_UNPROVEN;
    }

  /* Everything else -- the SFPU CC writers (SFPSETCC/SFPCOMPC/
     SFPPUSHC/SFPPOPC/compare-and-set mods), expander words whose
     delivered content lives elsewhere (MOP/MOP_CFG/REPLAY), loads,
     stores, packers, unaudited opcodes -- refuses.  Expander words are
     a TU-level question (the mop derivation's slot audit), never a
     word-level one.  */
  return RVTT_RAW_CC_UNPROVEN;
}

/* See rvtt-raw-boundary.h.  */

bool
rvtt_raw_cc_word_ambient_preserving_p (uint32_t word)
{
  return rvtt_raw_cc_word_class (word) != RVTT_RAW_CC_UNPROVEN;
}
