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
#include "rvtt-protos.h"
#include "rvtt-effects.h"
#include "rvtt-macro-tables.h"
#include "rvtt-mop-tables.h"
#include "rvtt-mop-derive.h"
#include "rvtt-raw-boundary.h"

/* Classification of raw `.ttinsn' instruction words.  Raw words come from
   inline asm in LLK code; each optimization that must reason about them
   asks one question per word (can it write an allocatable LREG, disturb
   the lane-enable state, write a PRGM register, touch the load-macro or
   address-modifier state, or is it a plain Dst counter write?).  The
   answers come from one table, rvtt_word_facts_classify, keyed on the
   opcode and the relevant fields.  Every word not listed there gets the
   conservative answer.  Only targets with a capability table (WH, BH)
   classify anything; on other targets every word is unknown.

   A SETRWC word is a pure Dst counter write (see SETRWC.md and
   setrwc_decode) only if: clear_ab_vld is 0 (it clears the SrcA/SrcB
   data-valid bits, a cross-thread handshake); bit_mask is exactly the Dst
   bit (2), since the SrcA/SrcB bits write other counters, the fidelity
   bit resets the fidelity phase and the top two bits are undocumented;
   the SrcA/SrcB bits of rwc_cr are 0; and rwc_a/rwc_b are 0 (they are
   unused under a Dst-only mask, but are required to be 0 anyway).  */

/* Return true if S is the asm template `.ttinsn %0'.  */

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

/* If INSN is a `.ttinsn %0' asm with no outputs and a single constant
   input (the form of the TTI_ macros in ckernel_ops.h), store the
   instruction word in *WORD and return true.  Return false for any other
   insn.  */

static bool
raw_ttinsn_const_word (rtx_insn *insn, uint32_t *word)
{
  if (!insn || !NONDEBUG_INSN_P (insn) || CALL_P (insn) || JUMP_P (insn))
    return false;
  rtx pat = PATTERN (insn);
  /* A SET or PARALLEL around the asm means outputs or clobbers.  */
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

/* See rvtt-raw-boundary.h.  */

bool
rvtt_raw_ttinsn_word (rtx_insn *insn, uint32_t *word)
{
  return raw_ttinsn_const_word (insn, word);
}

/* See rvtt-raw-boundary.h.  Compare the opcode byte of WORD with the
   target's REPLAY encoding.  Return true on other targets, which is the
   conservative answer for every caller.  */

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
  if (!rvtt_word_pure_dst_rwc_p (word))
    return false;

  /* Describe it as the typed TTSETRWC does: a SET of the Dst counter.
     SETRWC is a SET even in its CR-relative modes; consumers treat it as
     a run separator, never as an increment that a row can absorb.  */
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
  /* One constant input, no outputs, clobbers or labels.  */
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
  return rvtt_word_pure_dst_rwc_p ((uint32_t) value);
}

/* Fill F with the facts for WORD.  See rvtt-raw-boundary.h for the
   meaning of each field.  The facts come from the functional models in
   the Tensix ISA documentation (the .md files named below) and the MOP
   facts in rvtt-mop-tables.h.  The accessors below answer:

     rvtt_word_lreg_class:     can the word write an allocatable LREG?
     rvtt_raw_cc_word_class:   can it disturb the all-lanes enable state?
     rvtt_mop_audited_word_p:  can it write a PRGM constant register,
			       LaneConfig or CC state?
     rvtt_word_init_class:     can it write LoadMacroConfig, launch a
			       macro or replay recorded content?
     rvtt_word_pure_dst_rwc_p: is it a pure Dst counter write?

   Supporting a new opcode means adding one case below.  */

void
rvtt_word_facts_classify (uint32_t word, rvtt_word_facts *f)
{
  /* Conservative defaults, used for any word not listed below.  */
  f->row = RVTT_WF_UNAUDITED;
  f->is_mop = false;
  f->is_mop_cfg = false;
  f->is_replay = false;
  f->loadi_dest = 0;
  f->lreg_inert = false;
  f->lreg_loadi_dest_rule = false;
  f->lreg_config_class = false;
  f->cc = RVTT_RAW_CC_UNPROVEN;
  f->prgm_ok = false;
  f->prgm_claim_mask = 0;
  f->prgm_why = "unaudited raw opcode";
  f->init_ok = false;
  f->pure_dst_rwc = false;

  unsigned opcode = word >> 24;

  /* Tensix NOPs.  0x00 executes nothing; 0x02 is dropped at the
     instruction FIFO.  MOP templates fill unused slots with these.  */
  if (opcode == 0x00 || opcode == 0x02)
    {
      f->row = RVTT_WF_NOP;
      f->lreg_inert = true;
      f->cc = RVTT_RAW_CC_INERT;
      f->prgm_ok = true;
      f->prgm_why = nullptr;
      f->init_ok = true;
      return;
    }

  /* MOP has no effects of its own: it expands the words previously
     written to the MOP template registers, so its effects are those of
     the template.  The accessors flag it (is_mop) so that callers check
     the template instead.  */
  if (opcode == XTT_MOP_OPCODE)
    {
      f->row = RVTT_WF_MOP;
      f->is_mop = true;
      f->lreg_inert = true;
      f->prgm_ok = true;
      f->prgm_why = nullptr;
      f->init_ok = true;
      return;
    }

  /* MOP_CFG writes only the high half of the MOP zmask.  No CC fact is
     recorded for it.  */
  if (opcode == XTT_MOP_CFG_OPCODE)
    {
      f->row = RVTT_WF_MOP_CFG;
      f->is_mop_cfg = true;
      f->lreg_inert = true;
      f->prgm_ok = true;
      f->prgm_why = nullptr;
      f->init_ok = true;
      return;
    }

  /* REPLAY plays back recorded content that the word does not show, so
     nothing is known about it.  */
  if (opcode == XTT_REPLAY_OPCODE)
    {
      f->row = RVTT_WF_REPLAY;
      f->is_replay = true;
      return;
    }

  /* MOVA2D (0x12) and ELWADD (0x28) use only the matrix-unit data path
     (Src banks, Dst rows, RWCs) for every field value (MOVA2D.md,
     ELWADD.md).  */
  if (opcode == 0x12 || opcode == 0x28)
    {
      f->row = RVTT_WF_MATRIX;
      f->lreg_inert = true;
      f->cc = RVTT_RAW_CC_INERT;
      f->prgm_ok = true;
      f->prgm_why = nullptr;
      f->init_ok = true;
      return;
    }

  /* CLEARDVALID (0x36) affects only the Src bank-valid bits
     (CLEARDVALID.md).  */
  if (opcode == 0x36)
    {
      f->row = RVTT_WF_CLEARDVALID;
      f->lreg_inert = true;
      f->cc = RVTT_RAW_CC_INERT;
      f->prgm_ok = true;
      f->prgm_why = nullptr;
      f->init_ok = true;
      return;
    }

  /* SETRWC (0x37) affects only the RWCs and bank-valid bits
     (SETRWC.md).  It is a pure Dst counter write under the field
     conditions described at the top of this file.  */
  if (opcode == 0x37)
    {
      f->row = RVTT_WF_SETRWC;
      f->lreg_inert = true;
      f->cc = RVTT_RAW_CC_INERT;
      f->prgm_ok = true;
      f->prgm_why = nullptr;
      f->init_ok = true;
      rvtt_macro::setrwc_fields sf;
      f->pure_dst_rwc = (rvtt_macro::setrwc_decode (word, &sf)
			 && sf.clear_ab_vld == 0
			 && sf.bit_mask == 0x4
			 && (sf.rwc_cr & 0x3) == 0
			 && sf.rwc_a == 0 && sf.rwc_b == 0);
      return;
    }

  /* INCRWC (0x38) only increments the RWCs (INCRWC.md).  Only the
     load-macro question is answered for it.  */
  if (opcode == 0x38)
    {
      f->row = RVTT_WF_INCRWC;
      f->init_ok = true;
      return;
    }

  /* SFPLOADI (0x71) writes LReg[VD] (bits 23:20) or, when VD >= 12 and
     backdoor loads are enabled, a LoadMacroConfig instruction template;
     never the lane flags (SFPLOADI.md, SFPCONFIG.md
     DISABLE_BACKDOOR_LOAD).  The LREG accessor checks the destination
     against the caller's mask; only allocatable destinations (< 8) are
     PRGM-safe.  */
  if (opcode == 0x71)
    {
      f->row = RVTT_WF_SFPLOADI;
      f->loadi_dest = (word >> 20) & 0xf;
      f->lreg_loadi_dest_rule = true;
      f->cc = RVTT_RAW_CC_INERT;
      f->prgm_ok = f->loadi_dest < 8;
      f->prgm_why = f->prgm_ok ? nullptr
	: "raw SFPLOADI with non-allocatable destination";
      f->init_ok = true;
      return;
    }

  /* SFPENCC (0x8A) writes the lane enables.  Only the exact all-lanes
     encoding (rvtt_macro::sfpencc_all_lanes_word: imm12
     SFPENCC_IMM12_BOTH, mod1 SFPENCC_MOD1_EI_RI, VD 0) is recognised
     (SFPENCC.md).  */
  if (opcode == 0x8A)
    {
      f->row = RVTT_WF_SFPENCC;
      f->cc = word == rvtt_macro::sfpencc_all_lanes_word ()
	? RVTT_RAW_CC_ALL_LANES : RVTT_RAW_CC_UNPROVEN;
      return;
    }

  /* SFPCAST (0x90) and SFPSTOCHRND (0x8E) write only LReg[VD] (when
     VD < 8 or VD == 16).  With VD >= 12 and backdoor loads enabled the
     word is instead stored into LoadMacroConfig.InstructionTemplate[VD-12]
     and not executed.  Neither case touches the lane enables
     (SFPCAST_*.md, SFPSTOCHRND_*.md, SFPCONFIG.md).  */
  if (opcode == 0x90 || opcode == 0x8E)
    {
      f->row = RVTT_WF_SFPCAST;
      f->cc = RVTT_RAW_CC_INERT;
      return;
    }

  /* SFPCONFIG (0x91) writes LREGs only for VD 11..14 (constant
     registers, never L0-L7) and LaneConfig, which holds the ROW_MASK
     bits, only for VD == 15; it never writes LaneFlags or
     UseLaneFlagsForLaneEnable (SFPCONFIG.md).  The LREG accessor
     rejects all SFPCONFIG words in its strict modes, because such a word
     could change a constant register or lane state that hoisted code
     relies on.

     For VD == 15 only the word with MOD1_IMM16_IS_VALUE set and imm16 0
     (TTI_SFPCONFIG (0, 0xF, 1), 0x910000F1) is accepted: applied to the
     default LaneConfig, every mod1 variant of it leaves LaneConfig at
     its default.  It is not a reset, though: the write is gated per
     column by LaneFlags, and the OR/XOR/lane-mask variants leave a set
     ROW_MASK bit set.  A nonzero imm16 can set ROW_MASK bits, and with
     bit 0 of mod1 clear the value comes from LReg[0].  For other VD the
     word claims that PRGM destination.  */
  if (opcode == 0x91)
    {
      f->row = RVTT_WF_SFPCONFIG;
      f->lreg_config_class = true;
      unsigned dest = (word >> 4) & 0xf;
      bool default_reset = (word & 1) == 1 && ((word >> 8) & 0xffff) == 0;
      if (dest != 15)
	{
	  f->cc = RVTT_RAW_CC_INERT;
	  f->prgm_ok = true;
	  f->prgm_why = nullptr;
	  f->prgm_claim_mask = 1u << dest;
	}
      else if (default_reset)
	{
	  f->cc = RVTT_RAW_CC_ALL_LANES;
	  f->prgm_ok = true;
	  f->prgm_why = nullptr;
	}
      else
	f->prgm_why = "raw SFPCONFIG writes LaneConfig";
      return;
    }

  /* Vector Unit value instructions.  According to VectorUnit.md, the
     only writers of LaneFlags are SFPENCC, SFPSETCC, SFPLE, SFPGT,
     SFPIADD, SFPLZ and SFPEXEXP; of the flag stack SFPPUSHC, SFPPOPC,
     SFPCOMPC and some SFPLE/SFPGT modes; of UseLaneFlagsForLaneEnable
     SFPENCC; and of ROW_MASK SFPCONFIG.  So the opcodes below leave the
     lane state alone, the conditional ones when mod1 selects none of
     their CC variants: SFPEXEXP without SET_CC_SGN_EXP (2) or
     SET_CC_COMP_EXP (8); SFPIADD with CC_NONE (4) and without CC_GTE0
     (8); SFPLZ without CC_NE0 (2) or CC_COMP (8); SFPLE/SFPGT without
     SET_CC (1) or MUTATE_STACK (2).  A word stored into a LoadMacroConfig
     template (VD >= 12) does not touch lane state either.  SFPLOADMACRO
     is absent because it runs template instructions the word does not
     show.  Only the CC question is answered: these words write LREGs.  */
  {
    unsigned mod1 = word & 0xf;
    bool lanes_untouched;
    switch (opcode)
      {
      case 0x70: /* SFPLOAD */
      case 0x72: /* SFPSTORE */
      case 0x73: /* SFPLUT */
      case 0x74: /* SFPMULI */
      case 0x75: /* SFPADDI */
      case 0x76: /* SFPDIVP2 */
      case 0x78: /* SFPEXMAN */
      case 0x7A: /* SFPSHFT */
      case 0x7C: /* SFPMOV */
      case 0x7D: /* SFPABS */
      case 0x7E: /* SFPAND */
      case 0x7F: /* SFPOR */
      case 0x80: /* SFPNOT */
      case 0x82: /* SFPSETEXP */
      case 0x83: /* SFPSETMAN */
      case 0x84: /* SFPMAD */
      case 0x85: /* SFPADD */
      case 0x86: /* SFPMUL */
      case 0x89: /* SFPSETSGN */
      case 0x8C: /* SFPTRANSP */
      case 0x8D: /* SFPXOR */
      case 0x8F: /* SFPNOP */
      case 0x92: /* SFPSWAP */
      case 0x94: /* SFPSHFT2 */
      case 0x95: /* SFPLUTFP32 */
      case 0x98: /* SFPMUL24 */
      case 0x99: /* SFPARECIP */
	lanes_untouched = true;
	break;
      case 0x77: /* SFPEXEXP */
      case 0x81: /* SFPLZ */
	lanes_untouched = (mod1 & 0xa) == 0;
	break;
      case 0x79: /* SFPIADD */
	lanes_untouched = (mod1 & 0xc) == 0x4;
	break;
      case 0x96: /* SFPLE */
      case 0x97: /* SFPGT */
	lanes_untouched = (mod1 & 0x3) == 0;
	break;
      default:
	lanes_untouched = false;
	break;
      }
    if (lanes_untouched)
      {
	f->row = RVTT_WF_SFPU_VALUE;
	f->cc = RVTT_RAW_CC_INERT;
	return;
      }
  }

  /* Synchronization instructions (0xA0..0xA7: ATGETM, ATRELM, SEMINIT,
     SEMPOST, SEMGET, SEMWAIT, STALLWAIT, ...) affect only semaphores,
     mutexes and stalls.  */
  if (opcode >= 0xA0 && opcode <= 0xA7)
    {
      f->row = RVTT_WF_SYNC;
      f->lreg_inert = true;
      f->cc = RVTT_RAW_CC_INERT;
      f->prgm_ok = true;
      f->prgm_why = nullptr;
      f->init_ok = true;
      return;
    }

  /* Configuration instructions (0xB0..0xB8: SETC16, WRCFG, RDCFG,
     RMWCIB, ...) write the thread and backend configuration spaces,
     which are separate from the SFPU lane state, the LREGs and the
     SFPCONFIG state (SETC16.md).  rvtt_word_init_class separately
     tracks SETC16 writes to the address-modifier registers the planner
     owns.  */
  if (opcode >= 0xB0 && opcode <= 0xB8)
    {
      f->row = RVTT_WF_THREAD_CFG;
      f->lreg_inert = true;
      f->cc = RVTT_RAW_CC_INERT;
      f->prgm_ok = true;
      f->prgm_why = nullptr;
      f->init_ok = true;
      return;
    }

  /* Everything else, including the CC writers, keeps the defaults.  */
}

/* The accessors below combine the word facts with the caller's
   context and name the refusal on failure.  */

/* See rvtt-raw-boundary.h.  */

rvtt_wf_lreg_verdict
rvtt_word_lreg_class (uint32_t word, unsigned contract_mask,
		      bool region_strict, bool config_strict)
{
  rvtt_wf_lreg_verdict v = { true, false, false, nullptr };
  rvtt_word_facts f;
  rvtt_word_facts_classify (word, &f);
  if (f.is_mop)
    v.is_mop = true;
  else if (f.is_replay)
    {
      v.ok = false;
      v.is_replay = true;
      v.why = "crosscall-caller-replay-unproven";
    }
  else if (f.lreg_loadi_dest_rule)
    {
      if ((contract_mask >> f.loadi_dest) & 1)
	{
	  v.ok = false;
	  v.why = "crosscall-caller-word-unproven";
	}
    }
  else if (f.lreg_config_class)
    {
      if (region_strict || config_strict)
	{
	  v.ok = false;
	  v.why = "crosscall-caller-config-word-unproven";
	}
    }
  else if (!f.lreg_inert)
    {
      v.ok = false;
      v.why = "crosscall-caller-word-unproven";
    }
  return v;
}

/* See rvtt-raw-boundary.h.  The SETC16 and SFPCONFIG opcodes come from
   the target capabilities C, so they are checked here rather than in
   the table.  */

rvtt_wf_init_verdict
rvtt_word_init_class (uint32_t word, const rvtt_init_hoist_program &prog,
		      const rvtt_macro::caps *c)
{
  rvtt_wf_init_verdict v = { true, false, false, word, true, nullptr };
  unsigned opcode = word >> 24;
  if (c && opcode == c->setc16_opcode)
    {
      unsigned reg, value;
      if (!rvtt_macro::decode_setc16 (c, word, &reg, &value))
	{
	  v.ok = false;
	  v.why = "drain-init-ownership-unproven";
	}
      else
	for (unsigned i = 0; i != prog.n_setc16; ++i)
	  if (prog.setc16[i].reg == reg)
	    v.owned_row_write = true;
    }
  else if (c && opcode == c->sfpconfig_opcode)
    {
      /* SFPCONFIG may write LoadMacroConfig or LaneConfig.  */
      v.ok = false;
      v.why = "drain-init-ownership-unproven";
    }
  else
    {
      rvtt_word_facts f;
      rvtt_word_facts_classify (word, &f);
      v.is_mop = f.is_mop;
      if (!f.init_ok)
	{
	  v.ok = false;
	  v.why = "drain-init-ownership-unproven";
	}
    }
  return v;
}

/* See rvtt-raw-boundary.h.  Targets without a capability table answer
   RVTT_RAW_CC_UNPROVEN for every word.  */

rvtt_raw_cc_class
rvtt_raw_cc_word_class (uint32_t word)
{
  rvtt_raw_cc_class r = RVTT_RAW_CC_UNPROVEN;
  rvtt_macro::cpu_t cpu = TARGET_XTT_TENSIX_BH ? rvtt_macro::CPU_BH
    : TARGET_XTT_TENSIX_WH ? rvtt_macro::CPU_WH : rvtt_macro::CPU_QSR;
  if (rvtt_macro_caps_for_cpu (cpu))
    {
      rvtt_word_facts f;
      rvtt_word_facts_classify (word, &f);
      r = f.cc;
    }
  return r;
}

/* See rvtt-raw-boundary.h.  Targets without a capability table answer
   false.  */

bool
rvtt_word_pure_dst_rwc_p (uint32_t word)
{
  bool r = false;
  rvtt_macro::cpu_t cpu = TARGET_XTT_TENSIX_BH ? rvtt_macro::CPU_BH
    : TARGET_XTT_TENSIX_WH ? rvtt_macro::CPU_WH : rvtt_macro::CPU_QSR;
  if (rvtt_macro_caps_for_cpu (cpu))
    {
      rvtt_word_facts f;
      rvtt_word_facts_classify (word, &f);
      r = f.pure_dst_rwc;
    }
  return r;
}

/* See rvtt-mop-derive.h.  IN_SLOT is true for a word written into a MOP
   template slot; ST, if non-null, collects MOP words whose check is
   deferred to rvtt_mop_derive_finish.  */

bool
rvtt_mop_audited_word_p (uint32_t word, unsigned *claimed, const char **why,
			 rvtt_mop_derive_state *st, bool in_slot)
{
  bool ok = true;
  const char *w = nullptr;
  unsigned claim = 0;
  bool push_mop = false;
  rvtt_word_facts f;
  rvtt_word_facts_classify (word, &f);
  if (f.is_mop || f.is_mop_cfg)
    {
      if (in_slot)
	{
	  /* A MOP word inside a template would re-enter the expander;
	     its behaviour is not documented.  */
	  w = "mop-template-nested-unproven: MOP word in a template slot";
	  ok = false;
	}
      else if (!st)
	{
	  w = "unaudited raw opcode";
	  ok = false;
	}
      else
	/* The effects are in the template slots, which
	   rvtt_mop_derive_finish checks once all slot writes are
	   known.  MOP_CFG has no effect of interest.  */
	push_mop = f.is_mop;
    }
  else if (f.is_replay && in_slot)
    {
      /* The slot would play back replay-buffer content we cannot
	 see.  */
      w = "mop-template-replay-unproven: REPLAY word in a template slot";
      ok = false;
    }
  else if (!f.prgm_ok)
    {
      w = f.prgm_why;
      ok = false;
    }
  else
    claim = f.prgm_claim_mask;

  if (!ok)
    {
      *why = w;
      return false;
    }
  *claimed |= claim;
  if (push_mop && st)
    st->mop_pushed = true;
  return true;
}

/* See rvtt-raw-boundary.h.  */

bool
rvtt_raw_cc_word_ambient_preserving_p (uint32_t word)
{
  return rvtt_raw_cc_word_class (word) != RVTT_RAW_CC_UNPROVEN;
}

/* See rvtt-raw-boundary.h.  Opcodes whose CC class depends on other
   fields of the word are never inert by opcode alone.  */

bool
rvtt_raw_cc_opcode_inert_p (unsigned opcode)
{
  switch (opcode)
    {
    case 0x77: /* SFPEXEXP */
    case 0x79: /* SFPIADD */
    case 0x81: /* SFPLZ */
    case 0x8A: /* SFPENCC */
    case 0x91: /* SFPCONFIG */
    case 0x96: /* SFPLE */
    case 0x97: /* SFPGT */
      return false;
    default:
      return (opcode < 0x100
	      && rvtt_raw_cc_word_class (opcode << 24) == RVTT_RAW_CC_INERT);
    }
}
