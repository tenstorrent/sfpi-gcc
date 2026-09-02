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
   (the typecast walk-transparency class).  The question is the
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
   architecturally swallowed (stored, not executed --
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

/* ================================================================== */
/* THE unified audited word-fact table.

   One recorded-fact table `word -> {LREG, CC, PRGM, ADDR_MOD, Dst/RWC}
   face verdicts', replacing the four parallel audited-word classifiers
   that grew up mirroring each other -- the LREG and init faces of
   gimple-rvtt-crosscall.cc (classify_word_lreg / classify_word_init),
   the CC face above (rvtt_raw_cc_word_class), and the PRGM face of the
   TU freedom proof (rvtt_mop_audited_word_p, declared in
   rvtt-mop-derive.h) -- plus the pure-Dst/RWC word test.  Every face
   asks a DIFFERENT architectural question of the same word, so the
   verdicts stay per-face; but the opcode-class decode, the field
   extraction, and the recorded provenance now live in exactly one
   place: the row blocks of rvtt_word_facts_classify
   (rvtt-raw-boundary.cc).  A new audited opcode is ONE row block there
   (all five face verdicts + provenance keys), never 3-4 coordinated
   edits.

   TABLE PROPERTY (the preserving-vs-kill doctrine above, now binding
   on the table itself): every face's proven verdict means
   ambient-PRESERVING only -- never a KILL/GEN fact.  A raw word can
   sit swallowed in a REPLAY record window (stored, not executed --
   rvtt-mop-tables.h), so its EXECUTION can never be asserted
   from the word alone; only effects sound under BOTH the executed and
   the swallowed reading are recordable in a row.  The vocabulary
   enforces it: face verdicts are `proven inert / proven
   ambient-establishing / UNPROVEN'; no accessor exposes, and no row
   may ever record, a kill face.

   Known face ASYMMETRIES, preserved verdict-identically from the four
   legacy classifiers (per-face fidelity beats cross-face tidiness;
   widening any of them is an owner adjudication, never a merge edit):
     - INCRWC (0x38) is admitted on the ADDR_MOD face only; the LREG
       and PRGM faces never audited it and keep refusing it.
     - SFPCAST/SFPSTOCHRND (0x90/0x8E) are CC-INERT (the
       audit) but stay unaudited on the LREG/PRGM/ADDR_MOD faces.
     - SFPENCC's word-exact all-lanes form is a CC-face fact only.  */

/* The audited opcode-class row a word decodes to.  Exactly one row per
   recorded architectural fact class; RVTT_WF_UNAUDITED is the refusing
   default row (every face refuses).  */

enum rvtt_wf_row
{
  RVTT_WF_UNAUDITED = 0,
  RVTT_WF_NOP,			/* 0x00 zero word / 0x02 FIFO NOP    */
  RVTT_WF_MOP,			/* XTT_MOP_OPCODE		     */
  RVTT_WF_MOP_CFG,		/* XTT_MOP_CFG_OPCODE		     */
  RVTT_WF_REPLAY,		/* XTT_REPLAY_OPCODE		     */
  RVTT_WF_MATRIX,		/* 0x12 MOVA2D / 0x28 ELWADD	     */
  RVTT_WF_CLEARDVALID,		/* 0x36				     */
  RVTT_WF_SETRWC,		/* 0x37				     */
  RVTT_WF_INCRWC,		/* 0x38				     */
  RVTT_WF_SFPLOADI,		/* 0x71				     */
  RVTT_WF_SFPENCC,		/* 0x8A				     */
  RVTT_WF_SFPCAST,		/* 0x90 SFPCAST / 0x8E SFPSTOCHRND   */
  RVTT_WF_SFPCONFIG,		/* 0x91				     */
  RVTT_WF_SYNC,			/* 0xA0..0xA7			     */
  RVTT_WF_THREAD_CFG		/* 0xB0..0xB8 (SETC16 and neighbours)*/
};

/* The recorded facts of one word: its row, the decoded fields the
   field-sensitive rows pin, and one verdict per face.  Target
   capability gates (the CC and Dst/RWC faces refuse everything on a
   CPU without a capability table) and consumer context (contract
   masks, strictness stances, slot discipline, owned rows) are applied
   by the face accessors -- the facts here are pure functions of the
   word.  */

struct rvtt_word_facts
{
  rvtt_wf_row row;

  /* Row-identity deferral flags (context applied at the faces).  */
  bool is_mop;			/* effects live in the template file */
  bool is_mop_cfg;		/* zmask high half only		     */
  bool is_replay;		/* plays back recorded content	     */

  /* Decoded fields (valid per row; refusing defaults elsewhere).  */
  unsigned loadi_dest;		/* SFPLOADI bits 23:20		     */

  /* LREG face: can the word write an ALLOCATABLE hard LREG?  */
  bool lreg_inert;		/* unconditionally proven inert	     */
  bool lreg_loadi_dest_rule;	/* SFPLOADI: writes LREG loadi_dest  */
  bool lreg_config_class;	/* SFPCONFIG: inert, but refused
				   under region/config-strict stances */

  /* CC face: lane-enable classification (pre-capability-gate).  */
  rvtt_raw_cc_class cc;

  /* PRGM face: PRGM register / LaneConfig / CC write audit.  */
  bool prgm_ok;
  unsigned prgm_claim_mask;	/* SFPCONFIG: claimed PRGM dest	     */
  const char *prgm_why;		/* refusal name when !prgm_ok	     */

  /* ADDR_MOD/init face: LoadMacroConfig / launch / replay audit
     (the caps-keyed SETC16/SFPCONFIG opcodes are the accessor's).  */
  bool init_ok;

  /* Dst/RWC face: field-pure Dst-leg RWC counter write (the typed
     TTSETRWC mirror; the run/row-separator class).  */
  bool pure_dst_rwc;
};

extern void rvtt_word_facts_classify (uint32_t word, rvtt_word_facts *f);

/* ------------------------------------------------------------------ */
/* Per-face query accessors.  The CC face accessor is
   rvtt_raw_cc_word_class above (name unchanged); the PRGM face
   accessor is rvtt_mop_audited_word_p (declaration stays in
   rvtt-mop-derive.h with its state type; body now lives here, over
   the table).  */

/* LREG face verdict (the cross-call contract scan's vocabulary).  */

struct rvtt_wf_lreg_verdict
{
  bool ok;			/* audited contract-LREG-inert	     */
  bool is_mop;			/* defer to the TU template audit    */
  bool is_replay;		/* recorded content: refuse	     */
  const char *why;
};

extern rvtt_wf_lreg_verdict
rvtt_word_lreg_class (uint32_t word, unsigned contract_mask,
		      bool region_strict = false,
		      bool config_strict = false);

/* ADDR_MOD/init face verdict (the init-hoist ownership scan's
   vocabulary).  */

struct rvtt_init_hoist_program;
namespace rvtt_macro { struct caps; }

struct rvtt_wf_init_verdict
{
  bool ok;
  bool is_mop;
  bool owned_row_write;		/* SETC16-class write to an owned row */
  uint32_t word;		/* the resolved word (constant only)  */
  bool word_exact;
  const char *why;
};

extern rvtt_wf_init_verdict
rvtt_word_init_class (uint32_t word, const rvtt_init_hoist_program &prog,
		      const rvtt_macro::caps *c);

/* Dst/RWC face word-level test (the rvtt_raw_pure_dst_rwc extractors
   above apply it after canonical-word extraction).  */

extern bool rvtt_word_pure_dst_rwc_p (uint32_t word);

#endif /* GCC_RVTT_RAW_BOUNDARY_H */
