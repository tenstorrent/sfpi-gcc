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

/* The LLK library issues some instructions as raw constant `.ttinsn'
   words (the TTI_ macro shape of ckernel_ops.h) rather than through typed
   builtins, so the late analyses must derive what such a word does from
   its encoding or refuse.  Words are classified by opcode and field
   class against the capability-table encoding, never by matching whole
   words.

   rvtt_raw_pure_dst_rwc recognises the canonical single-constant
   `.ttinsn %0' asm whose word is a SETRWC that writes only the Dst RWC
   counter pair (no SrcA/SrcB counter or bank-valid effect, no
   fidelity-phase reset, no LREG, CC, Dst-memory or configuration
   effect).  This is the run/row separator that the typed rvtt_ttsetrwc
   and rvtt_ttdstface patterns also express, and the effect set returned
   is the same as the typed TTSETRWC one.  Every other word stays opaque,
   so consumers refuse under their own names.

   Return true and fill *RWC (kind SET, the Dst-only set_mask) when INSN
   is such a word.  QSR has no capability table and always refuses.  */

extern bool rvtt_raw_pure_dst_rwc (rtx_insn *insn, xtt_rwc_effect_t *rwc);

/* The same test for a GIMPLE_ASM statement STMT.  */
extern bool rvtt_raw_pure_dst_rwc_gimple (const gimple *stmt);

/* If INSN is a canonical raw word (the TTI_ macro shape: an
   output-and-clobber-free `.ttinsn %0' asm with one constant input),
   store the word in *WORD and return true.  The word is not classified;
   callers apply their own classification.  */
extern bool rvtt_raw_ttinsn_word (rtx_insn *insn, uint32_t *word);

/* Return true if WORD is a replay-owning opcode according to the target
   encoding table.  Targets without a table answer true, the conservative
   direction.  */
extern bool rvtt_raw_replay_owner_word_p (uint32_t word);

/* Classification of a raw instruction word by whether it can disturb
   the all-lanes lane-enable state (the per-lane LaneFlags /
   UseLaneFlagsForLaneEnable pair written by SFPENCC and friends, plus the
   LaneConfig ROW_MASK lane-predication bits):

     RVTT_RAW_CC_INERT       -- the word leaves lane-enable state untouched;
     RVTT_RAW_CC_ALL_LANES   -- the word writes lane-enable state, and from
				the all-lanes state it writes all-lanes back
				(the canonical SFPENCC word, or the
				imm16 == 0 LaneConfig word);
     RVTT_RAW_CC_UNPROVEN    -- everything else, including unknown
				opcodes, expander words whose payload is
				not in the word, and lane-enable writes of
				unknown value.

   Consumers may treat the two proven classes only as preserving an
   all-lanes state, never as resetting it: a raw word can sit inside a
   REPLAY record window, where it is stored rather than executed (see
   rvtt-mop-tables.h), so the word alone never proves that it executes.
   On QSR, which has no capability table, every word is UNPROVEN.  */

enum rvtt_raw_cc_class
{
  RVTT_RAW_CC_UNPROVEN = 0,
  RVTT_RAW_CC_INERT,
  RVTT_RAW_CC_ALL_LANES
};

extern rvtt_raw_cc_class rvtt_raw_cc_word_class (uint32_t word);

/* Return true if WORD is in either proven class above, i.e. it cannot
   take the lane-enable state away from all-lanes.  */
extern bool rvtt_raw_cc_word_ambient_preserving_p (uint32_t word);

/* Return true if every word with opcode OPCODE is CC-inert whatever its
   other fields; used for words whose operand fields are run-time
   values.  */
extern bool rvtt_raw_cc_opcode_inert_p (unsigned opcode);

/* The raw word fact table.

   rvtt_word_facts_classify (rvtt-raw-boundary.cc) decodes a word once
   into a row and a verdict for each of five questions ("faces"): LREG
   (rvtt_word_lreg_class), CC (rvtt_raw_cc_word_class), PRGM
   (rvtt_mop_audited_word_p, declared in rvtt-mop-derive.h), ADDR_MOD/init
   (rvtt_word_init_class) and pure Dst/RWC (rvtt_word_pure_dst_rwc_p).
   Supporting a new opcode means adding one row block there with all five
   verdicts.

   As for the CC face above, every proven verdict only says that the word
   preserves the relevant state, never that it kills or establishes it
   unconditionally, because a word inside a REPLAY record window is stored
   rather than executed.

   The faces are deliberately not symmetric:
     - INCRWC (0x38) is accepted on the ADDR_MOD face only.
     - SFPCAST/SFPSTOCHRND (0x90/0x8E) are CC-inert but unknown to the
       LREG, PRGM and ADDR_MOD faces.
     - The all-lanes SFPENCC word is a CC-face fact only.
     - The Vector Unit value instructions (RVTT_WF_SFPU_VALUE) are
       CC-inert only; they write LREGs, and with VD >= 12 they can write
       LoadMacroConfig, so the other faces refuse them.  */

/* The opcode class a word decodes to.  RVTT_WF_UNAUDITED is the default
   row, for which every face refuses.  */

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
  RVTT_WF_SFPU_VALUE,		/* Vector Unit value instructions    */
  RVTT_WF_SFPCONFIG,		/* 0x91				     */
  RVTT_WF_SYNC,			/* 0xA0..0xA7			     */
  RVTT_WF_THREAD_CFG		/* 0xB0..0xB8 (SETC16 and neighbours)*/
};

/* The facts of one word: its row, the fields the field-sensitive rows
   decode, and one verdict per face.  These depend only on the word; the
   face accessors apply target capability checks (the CC and Dst/RWC faces
   refuse everything without a capability table) and consumer context
   (contract masks, strictness, owned rows).  */

struct rvtt_word_facts
{
  rvtt_wf_row row;

  /* Rows whose effects depend on context the accessors supply.  */
  bool is_mop;			/* effects live in the template file */
  bool is_mop_cfg;		/* zmask high half only		     */
  bool is_replay;		/* plays back recorded content	     */

  /* Decoded fields (valid only for the rows that use them).  */
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

  /* ADDR_MOD/init face: LoadMacroConfig / launch / replay effects
     (SETC16 and SFPCONFIG depend on the capability table and are
     decided by the accessor).  */
  bool init_ok;

  /* Dst/RWC face: the word writes only the Dst RWC counters, like
     the typed TTSETRWC run/row separator.  */
  bool pure_dst_rwc;
};

extern void rvtt_word_facts_classify (uint32_t word, rvtt_word_facts *f);

/* Per-face accessors.  The CC face is rvtt_raw_cc_word_class above and
   the PRGM face is rvtt_mop_audited_word_p (rvtt-mop-derive.h).  */

/* LREG face verdict.  */

struct rvtt_wf_lreg_verdict
{
  bool ok;			/* leaves contract LREGs alone	     */
  bool is_mop;			/* defer to the TU template audit    */
  bool is_replay;		/* recorded content: refuse	     */
  const char *why;
};

extern rvtt_wf_lreg_verdict
rvtt_word_lreg_class (uint32_t word, unsigned contract_mask,
		      bool region_strict = false,
		      bool config_strict = false);

/* ADDR_MOD/init face verdict.  */

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

/* Dst/RWC face test on WORD; rvtt_raw_pure_dst_rwc applies it after
   extracting the word.  */

extern bool rvtt_word_pure_dst_rwc_p (uint32_t word);

#endif /* GCC_RVTT_RAW_BOUNDARY_H */
