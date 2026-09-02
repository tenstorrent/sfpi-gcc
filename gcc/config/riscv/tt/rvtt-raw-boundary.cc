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
   future CPU without a table) refuses everything.  These semantics
   are enforced by the SETRWC row of rvtt_word_facts_classify below
   (queried through the rvtt_word_pure_dst_rwc_p accessor).  */

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
  if (!rvtt_word_pure_dst_rwc_p (word))
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
  return rvtt_word_pure_dst_rwc_p ((uint32_t) value);
}

/* ================================================================== */
/* THE unified audited word-fact table (FABLE item #4, Deliverable B).

   See rvtt-raw-boundary.h for the contract, the table property
   (preserving-vs-kill doctrine), and the recorded face asymmetries.
   Discipline identical to every audited class this file has carried
   since lane IV: each row is a recorded architectural fact with
   provenance, classified by opcode/field derivation -- never by
   operation identity or trust in the emitting library.  Provenance
   keys (the shared vocabulary of the four pre-table classifiers this
   table replaced):

     [ISA]  tt-isa-documentation BlackholeA0+WormholeB0
	    TensixTile/TensixCoprocessor (the mandatory prior); the
	    named .md file carries the cited functional model.
     [SIM]  craq-sim src/tensix.cpp (pinned tree) executors.
     [MOPT] an audited fact recorded in rvtt-mop-tables.h with its own
	    provenance.

   The five faces and their consumers:

     LREG     gimple-rvtt-crosscall.cc contract scans (via
	      rvtt_word_lreg_class): can the word write an ALLOCATABLE
	      hard LREG in the contract set?
     CC	      the entry-ambient walk (via rvtt_raw_cc_word_class, lane
	      IV): can the word disturb the all-lanes lane-enable
	      ambient?
     PRGM     the TU freedom proof (via rvtt_mop_audited_word_p,
	      rvtt-mop-derive.cc / gimple-rvtt-prgm-const.cc): can the
	      word write a PRGM constant register, LaneConfig, or CC
	      state unaudited?
     ADDR_MOD the init-hoist ownership scan (via rvtt_word_init_class):
	      can the word write LoadMacroConfig, launch a macro, or
	      replay recorded content?  SETC16-class owned-row
	      tracking rides the accessor (caps are target context).
     Dst/RWC  the macro-planner run separator (via
	      rvtt_word_pure_dst_rwc_p): is the word a pure Dst-leg
	      RWC counter write?

   A new audited opcode becomes ONE row block below -- all five face
   verdicts plus provenance -- and nothing else.  */

void
rvtt_word_facts_classify (uint32_t word, rvtt_word_facts *f)
{
  /* Refusing defaults for every face (the RVTT_WF_UNAUDITED row).  */
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

  /* Tensix NOPs.  0x00 executes nothing; 0x02 is swallowed at the
     instruction FIFO and delivers nothing ([SIM] IS_TENSIX_NOP +
     tensix_push_inst_fifo early-return; [MOPT] NOP facts).  The
     production template constructors park unused slots on exactly
     this word (ckernel_template ctor TT_OP_NOP).  */
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

  /* MOP: no effects of its own -- it expands the instruction words
     previously programmed into the MOP template registers, so the
     effects live in OTHER stores ([MOPT] slot taxonomy).  Every face
     defers: LREG/ADDR_MOD admit with the is_mop deferral flag (the
     consumer runs the TU template audit); PRGM admits only under a
     derive state (the finish adjudication) and refuses in a template
     slot -- both applied at the accessor.  The word's own fields are
     expansion-count facts, never effect facts.  */
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

  /* MOP_CFG: writes only the persistent zmask high half -- a type-0
     iteration-count fact ([SIM] mop_cfg(); [MOPT]).  Inert on the
     LREG/PRGM/ADDR_MOD faces (PRGM's slot/state context applies at
     the accessor); no lane-enable fact is recorded (CC refuses).  */
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

  /* REPLAY: plays back recorded content the word does not carry --
     every face refuses (the LREG face with its own is_replay
     deferral name; PRGM's in-slot discipline names the slot case at
     the accessor).  */
  if (opcode == XTT_REPLAY_OPCODE)
    {
      f->row = RVTT_WF_REPLAY;
      f->is_replay = true;
      return;
    }

  /* MOVA2D (0x12) / ELWADD (0x28): matrix-unit data path only (Src
     banks, Dst rows, RWC bookkeeping); l_regs, lane predication, PRGM
     and thread-config state untouched for every field value.  [SPEC]
     specs/MOVA2D.md + ELWADD.md functional models; [SIM] tensix.cpp
     TENSIX_EXECUTE_MOVA2D / TENSIX_EXECUTE_ELWADD @ 9f324140 (full
     citations recorded at the [MOPT] facts).  */
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

  /* CLEARDVALID (0x36): Src bank-valid bookkeeping only.  [MOPT];
     [ISA] CLEARDVALID.md.  */
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

  /* SETRWC (0x37): RWC counter and bank-valid bookkeeping only.
     [MOPT]; [ISA] SETRWC.md.  The Dst/RWC face additionally demands
     architectural field purity (semantics at the architectural-purity
     comment atop this file: no clear_ab_vld, Dst-leg-only mask, no
     Src CR-mode bits, zero dead value fields) -- the run/row-separator
     class whose derived effect mirrors the typed TTSETRWC entry.  */
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

  /* INCRWC (0x38): RWC counter increments only ([ISA] INCRWC.md).
     Recorded fact on the ADDR_MOD face only (the init-hoist scan,
     lane CA); the LREG/PRGM/CC faces never audited it and keep their
     refusing defaults (asymmetry note in the header).  */
  if (opcode == 0x38)
    {
      f->row = RVTT_WF_INCRWC;
      f->init_ok = true;
      return;
    }

  /* SFPLOADI (0x71): writes LReg[VD] (bits 23:20) -- or, VD >= 12
     with backdoor loads enabled, the LoadMacroConfig instruction
     template -- never lane flags in either arm.  [ISA] SFPLOADI.md;
     SFPCONFIG.md DISABLE_BACKDOOR_LOAD row.  LREG face: writes
     loadi_dest (contract-mask check at the accessor).  PRGM face:
     allocatable destinations (< 8) only.  ADDR_MOD face: LREG staging
     only, admitted (lane-predication under the architectural
     outermost all-lanes contract).  */
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

  /* SFPENCC (0x8A): the lane-enable writer itself.  Only the
     word-exact canonical all-lanes encoding is proven (the same
     capability-table word the typed kill test and the synthesized
     enable stand on: rvtt_macro::sfpencc_all_lanes_word, imm12 ==
     SFPENCC_IMM12_BOTH, mod1 == SFPENCC_MOD1_EI_RI, VD == 0); every
     other field combination -- lanes-off, complement, VD >= 12
     capture forms -- refuses.  A CC-face fact only.  [ISA]
     SFPENCC.md; [SIM] TENSIX_EXECUTE_SFPENCC.  */
  if (opcode == 0x8A)
    {
      f->row = RVTT_WF_SFPENCC;
      f->cc = word == rvtt_macro::sfpencc_all_lanes_word ()
	? RVTT_RAW_CC_ALL_LANES : RVTT_RAW_CC_UNPROVEN;
      return;
    }

  /* SFPCAST (0x90) / SFPSTOCHRND (0x8E): every mode of both
     functional models writes only LReg[VD] (and only when VD < 8 ||
     VD == 16); with VD >= 12 and backdoor loads enabled the word is
     instead captured into LoadMacroConfig.InstructionTemplate[VD-12]
     and executes nothing -- the LLK load-macro template-programming
     idiom.  Lane-enable state is untouched under BOTH the executed
     and the captured reading, so the CC class needs no
     DISABLE_BACKDOOR_LOAD knowledge.  A CC-face fact only (asymmetry
     note in the header).  [ISA] SFPCAST_*.md, SFPSTOCHRND_*.md,
     SFPCONFIG.md DISABLE_BACKDOOR_LOAD row; [SIM] tensix.cpp SFPU
     dispatch lreg_dest 12..15 capture arm + TENSIX_EXECUTE_SFPCAST/
     SFP_STOCH_RND.  */
  if (opcode == 0x90 || opcode == 0x8E)
    {
      f->row = RVTT_WF_SFPCAST;
      f->cc = RVTT_RAW_CC_INERT;
      return;
    }

  /* SFPCONFIG (0x91): LReg writes exist solely in the VD 11..14 arm
     (constant registers, never allocatable L0-7); only the VD == 15
     arm writes LaneConfig (which holds the ROW_MASK predication
     bits).  [ISA] SFPCONFIG.md functional model (no arm writes
     LaneFlags/UseLaneFlagsForLaneEnable); [SIM]
     TENSIX_EXECUTE_SFPCONFIG @ 9f324140.

     LREG face: inert as a word fact, but the hoist-region and
     config-prefix stances refuse the whole class at the accessor (a
     region-delivered config word could rewrite a programmable
     constant register or the lane-enable state a hoisted
     materialization relies on; refusing default, no dest-field
     decode under those stances).

     CC/PRGM faces, VD == 15: the admitted class is the audited
     default-reset word (MOD1_IMM16_IS_VALUE set, imm16 == 0 --
     TTI_SFPCONFIG (0, 0xF, 1), 0x910000F1): every mod1 completion is
     LaneConfig-confined and the resulting LaneConfig is the default
     all-lanes, ROW_MASK == 0 state ([MOPT] LaneConfig default-reset
     fact, lane AR audit) -- an ambient-establishing write.  Near
     misses stay refused by class: imm16 != 0 can set
     ROW_MASK/behavior bits; mod1 bit0 == 0 takes the value from
     LReg[0] (unauditable from the word).  VD != 15 claims the decoded
     PRGM destination.

     ADDR_MOD face: any SFPCONFIG-class word is a
     LoadMacroConfig/LaneConfig writer -- refuses (the accessor keys
     the opcode off the target caps; caps are context, not word
     facts).  */
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

  /* Sync family (0xA0..0xA7: ATGETM/ATRELM/SEMINIT/SEMPOST/SEMGET/
     SEMWAIT/STALLWAIT/...): semaphore, mutex and stall plumbing -- no
     SFPU lane state, LREG, PRGM or config effect in any functional
     model.  [MOPT] sync-family fact; [ISA] SEMWAIT.md/STALLWAIT.  */
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

  /* Thread-config family (0xB0..0xB8: SETC16 and neighbours, plus the
     main-CFG WRCFG/RDCFG/RMWCIB arms): writes the backend thread /
     main configuration spaces, disjoint from the Vector Unit's lane
     state, the LREGs, and the SFPU-internal SFPCONFIG state (the
     simulator's separated thread_cfg / config / sfpu state stores).
     [MOPT] thread-config fact; [ISA] SETC16.md.  The ADDR_MOD face's
     SETC16 owned-row tracking (a recorded write to a contract-owned
     address-modifier row) is applied at the accessor, where the
     target caps pin the SETC16 opcode and field encoding.  */
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

  /* Everything else -- the SFPU CC writers (SFPSETCC/SFPCOMPC/
     SFPPUSHC/SFPPOPC/compare-and-set mods), loads, stores, packers,
     unaudited opcodes -- keeps the refusing default row.  */
}

/* ------------------------------------------------------------------ */
/* Per-face accessors.  Each applies its consumer context (contract
   masks, strictness stances, target capability gates, slot
   discipline, owned rows) over the word facts and names its own
   refusals.  */

/* See rvtt-raw-boundary.h: the LREG face accessor (formerly
   gimple-rvtt-crosscall.cc classify_word_lreg).  */

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

/* See rvtt-raw-boundary.h: the ADDR_MOD/init face accessor (formerly
   gimple-rvtt-crosscall.cc classify_word_init).  The caps-keyed
   SETC16/SFPCONFIG opcode checks precede the table row (target caps
   are context, not word facts).  */

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
      /* Any SFPCONFIG-class word: LoadMacroConfig/LaneConfig writer.  */
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

/* See rvtt-raw-boundary.h: the CC face accessor (lane IV contract,
   unchanged name and vocabulary).  The capability gate is this
   face's: QSR (and any future CPU without a table) answers UNPROVEN
   for every word.  */

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

/* See rvtt-raw-boundary.h: the Dst/RWC face accessor (formerly this
   file's static pure_dst_rwc_word_p).  Same capability gate as the CC
   face: no table, no admission.  */

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

/* See rvtt-mop-derive.h: the PRGM face accessor (formerly defined in
   rvtt-mop-derive.cc; the declaration stays there with its state
   type).  The template-slot discipline (IN_SLOT) and the derive-state
   deferral (ST) are scan context, applied here over the table row.  */

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
	  /* A MOP/MOP_CFG word inside a template slot re-enters the
	     expander from inside an expansion; no recorded fact pins
	     that behavior.  */
	  w = "mop-template-nested-unproven: MOP word in a template slot";
	  ok = false;
	}
      else if (!st)
	{
	  w = "unaudited raw opcode";
	  ok = false;
	}
      else
	/* Effects live in the template slots; admission is deferred
	   to rvtt_mop_derive_finish once every TU slot write is
	   audited.  MOP_CFG is unconditionally inert.  */
	push_mop = f.is_mop;
    }
  else if (f.is_replay && in_slot)
    {
      /* The slot word would play back recorded replay-buffer content;
	 auditing recorded content is a later increment.  */
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
