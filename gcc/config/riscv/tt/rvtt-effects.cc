/* Typed architectural effect sets for Tensix instructions (Layer 1).
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
#include "insn-codes.h"
#include "insn-attr.h"
#include "recog.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "tm_p.h"
#include "rvtt.h"
#include "rvtt-protos.h"
#include "rvtt-effects.h"
#include "rvtt-macro-tables.h"
#include "rvtt-raw-boundary.h"

/* Effect data lives once, in the generated attribute family of
   rvtt-cost.md; this file only resolves it against operands.  Every
   default is refusing: an unaudited pattern, a call, or asm yields
   opaque=true and consumers must refuse byte-identically.  The one
   audited asm exception is the raw `.ttinsn' constant word whose
   architectural field decode proves the pure Dst/RWC counter class
   (rvtt-raw-boundary.cc); it carries the typed TTSETRWC effect set.  */

static xtt_subunit_t
subunit_from_attr (enum xtt_subunit a)
{
  switch (a)
    {
    case XTT_SUBUNIT_NONE:   return XTT_SU_NONE;
    case XTT_SUBUNIT_SIMPLE: return XTT_SU_SIMPLE;
    case XTT_SUBUNIT_MAD:    return XTT_SU_MAD;
    case XTT_SUBUNIT_ROUND:  return XTT_SU_ROUND;
    case XTT_SUBUNIT_LOAD:   return XTT_SU_LOAD;
    case XTT_SUBUNIT_STORE:  return XTT_SU_STORE;
    case XTT_SUBUNIT_CFG:    return XTT_SU_CFG;
    case XTT_SUBUNIT_SYNC:   return XTT_SU_SYNC;
    }
  gcc_unreachable ();
}

/* Resolve an operand-position bitmask to a hard-LREG mask (L0..L15 plus
   LREG16).  Positions holding pseudos or non-LREG operands contribute no
   bits; post-RA every live LREG operand is a hard register.  */

static uint32_t
lreg_mask_of_positions (int position_mask)
{
  uint32_t mask = 0;
  for (int i = 0; i < recog_data.n_operands && (position_mask >> i); ++i)
    if ((position_mask >> i) & 1)
      {
	rtx op = recog_data.operand[i];
	/* SFPU_REG_P bounds the bit to the real L0-L7 file; the former
	   `<= 16' bound admitted regnos through 96 (v0) into a 16-bit
	   mask -- bit 16 lies outside the mask contract (latent, FH
	   audit FHN-5).  */
	if (REG_P (op) && HARD_REGISTER_P (op) && SFPU_REG_P (REGNO (op)))
	  mask |= 1u << (REGNO (op) - SFPU_REG_FIRST);
      }
  return mask;
}

/* The canonical no-increment load/store address mode.  This constant is
   target capability data; it migrates into the capability tables and
   must never be duplicated into pass decision logic.  */

static int
no_increment_address_mode ()
{
  if (TARGET_XTT_TENSIX_BH)
    return 7;
  if (TARGET_XTT_TENSIX_WH)
    return 3;
  return -1;			/* QSR: unproven -> refuse.  */
}

/* Public accessor (transp-involution formation shares the same
   capability fact; see the comment above).  */

int
rvtt_no_increment_address_mode ()
{
  return no_increment_address_mode ();
}

xtt_effect_set
rvtt_insn_effects (rtx_insn *insn)
{
  xtt_effect_set e = {};
  e.opaque = true;
  e.result_latency = -1;
  e.next_slot_stall = false;
  e.rwc.kind = xtt_rwc_effect_t::UNKNOWN;

  if (!insn || !NONDEBUG_INSN_P (insn) || CALL_P (insn))
    return e;
  if (asm_noperands (PATTERN (insn)) >= 0)
    {
      /* Raw `.ttinsn' constant words (the LLK TTI_ macro shape) are
	 field-decoded architecturally -- rvtt-raw-boundary.cc; only
	 the pure Dst/RWC counter class is on record.  A proven word
	 carries the typed TTSETRWC effect set verbatim: no LREG, CC,
	 configuration, or Dst-memory effect, a SET-class RWC counter
	 effect, sync sub-unit, no audited result latency.  Every
	 other asm keeps the refusing opaque default.  */
      xtt_rwc_effect_t rwc;
      if (rvtt_raw_pure_dst_rwc (insn, &rwc))
	{
	  e.rwc = rwc;
	  e.subunit = XTT_SU_SYNC;
	  e.opaque = false;
	}
      return e;
    }
  int code = recog_memoized (insn);
  if (code < 0 || get_attr_type (insn) != TYPE_TENSIX)
    return e;

  /* The numeric attributes are stored with a +1 bias (generated
     attributes cannot be negative); 0 means unaudited.  This function is
     the only decoder of that bias.  */
  int read_positions = get_attr_xtt_lreg_read_ops (insn) - 1;
  int write_positions = get_attr_xtt_lreg_write_ops (insn) - 1;
  enum xtt_cc_effect cc = get_attr_xtt_cc_effect (insn);
  enum xtt_config_effect cfg = get_attr_xtt_config_effect (insn);
  enum xtt_rwc_effect rwc = get_attr_xtt_rwc_effect (insn);

  /* Any unaudited field keeps the whole set opaque.  */
  if (read_positions < 0 || write_positions < 0
      || cc == XTT_CC_EFFECT_UNKNOWN
      || cfg == XTT_CONFIG_EFFECT_UNKNOWN
      || rwc == XTT_RWC_EFFECT_UNKNOWN)
    return e;

  if (read_positions || write_positions
      || cfg == XTT_CONFIG_EFFECT_DEST
      || rwc == XTT_RWC_EFFECT_INC
      || rwc == XTT_RWC_EFFECT_SET
      || cfg == XTT_CONFIG_EFFECT_READ
      || rwc == XTT_RWC_EFFECT_ADDR_MODE)
    extract_insn (insn);

  e.lreg_read = lreg_mask_of_positions (read_positions);
  e.lreg_write = lreg_mask_of_positions (write_positions);

  e.cc_read = (cc == XTT_CC_EFFECT_READ || cc == XTT_CC_EFFECT_READWRITE);
  e.cc_write = (cc == XTT_CC_EFFECT_WRITE || cc == XTT_CC_EFFECT_READWRITE);

  /* Lane-state proof of a CC write (the macro planner's ambient-enable
     obligation): provable only when the written value is word-exact
     against the architectural all-lanes SFPENCC encoding, derived once
     in the capability tables.  Reaching the admitted instruction's
     operands by recognized code follows the XTT_RWC_EFFECT_INC
     precedent below; every other CC writer keeps the refusing default
     (lane-state-unproved).  Operand mapping mirrors the emission
     pipeline verbatim: the rvtt_sfpencc template prints "%1, %0" and
     the assembler reads "SFPENCC imm12, mod1", so operand 1 is the
     encoded imm12 and operand 0 the encoded mod1 -- the identical
     operand roles the deleted quarantined pass proved against
     (SFPENCC_MOD1_EI_RI at operand 0, SFPENCC_IMM12_BOTH at
     operand 1).  */
  if (e.cc_write && !e.cc_read && code == CODE_FOR_rvtt_sfpencc)
    {
      extract_insn (insn);
      if (recog_data.n_operands >= 2
	  && CONST_INT_P (recog_data.operand[0])
	  && CONST_INT_P (recog_data.operand[1])
	  && INTVAL (recog_data.operand[0]) >= 0
	  && INTVAL (recog_data.operand[1]) >= 0)
	{
	  uint32_t word;
	  e.cc_write_all_lanes
	    = (rvtt_macro::sfpencc_encode
		 (UINTVAL (recog_data.operand[1]),
		  UINTVAL (recog_data.operand[0]), &word)
	       && word == rvtt_macro::sfpencc_all_lanes_word ());
	}
    }

  if (cfg == XTT_CONFIG_EFFECT_DEST || cfg == XTT_CONFIG_EFFECT_READ)
    {
      /* A non-constant destination is a typed access to a statically
	 unknown register: every destination is possibly touched.  */
      int pos = get_attr_xtt_config_dest_op (insn) - 1;
      uint32_t dests = ~0u;
      if (pos >= 0 && pos < recog_data.n_operands
	  && CONST_INT_P (recog_data.operand[pos])
	  && INTVAL (recog_data.operand[pos]) >= 0
	  && INTVAL (recog_data.operand[pos]) <= 15)
	dests = 1u << INTVAL (recog_data.operand[pos]);
      if (cfg == XTT_CONFIG_EFFECT_DEST)
	e.config_dests_written = dests;
      else
	e.config_dests_read = dests;
    }

  switch (rwc)
    {
    case XTT_RWC_EFFECT_NONE:
      e.rwc.kind = xtt_rwc_effect_t::NONE;
      break;
    case XTT_RWC_EFFECT_FACE:
      /* One face advance: two architectural Dst += 8 counter steps.  */
      e.rwc.kind = xtt_rwc_effect_t::FACE;
      e.rwc.dst_delta = 16;
      break;
    case XTT_RWC_EFFECT_INC:
      /* Class admitted by attribute; typed operand layout (cr, d, b, a)
	 reached by code.  */
      gcc_assert (code == CODE_FOR_rvtt_ttincrwc);
      if (!CONST_INT_P (recog_data.operand[0])
	  || !CONST_INT_P (recog_data.operand[1]))
	return e;
      e.rwc.kind = xtt_rwc_effect_t::INC;
      e.rwc.cr_delta = INTVAL (recog_data.operand[0]);
      e.rwc.dst_delta = INTVAL (recog_data.operand[1]);
      break;
    case XTT_RWC_EFFECT_SET:
      {
	int mask_pos = code == CODE_FOR_rvtt_ttsetrwc_qsr ? 3 : 5;
	if (!CONST_INT_P (recog_data.operand[mask_pos]))
	  return e;
	e.rwc.kind = xtt_rwc_effect_t::SET;
	e.rwc.set_mask = UINTVAL (recog_data.operand[mask_pos]);
      }
      break;
    case XTT_RWC_EFFECT_ADDR_MODE:
      {
	/* Load/store RWC effect decided by the address-mode operand.  */
	int pos = code == CODE_FOR_rvtt_sfpload_lv_int ? 8
	  : code == CODE_FOR_rvtt_sfpstore_int ? 6 : -1;
	int no_inc = no_increment_address_mode ();
	if (pos < 0 || no_inc < 0
	    || !CONST_INT_P (recog_data.operand[pos]))
	  e.rwc.kind = xtt_rwc_effect_t::UNKNOWN;
	else if (INTVAL (recog_data.operand[pos]) == no_inc)
	  e.rwc.kind = xtt_rwc_effect_t::NONE;
	else
	  /* Auto-increment deltas are capability-table data;
	     unaudited modes stay UNKNOWN so consumers refuse.  */
	  e.rwc.kind = xtt_rwc_effect_t::UNKNOWN;
      }
      break;
    case XTT_RWC_EFFECT_UNKNOWN:
      gcc_unreachable ();
    }

  e.subunit = subunit_from_attr (get_attr_xtt_subunit (insn));
  e.dst_mem_read = e.subunit == XTT_SU_LOAD;
  e.dst_mem_write = e.subunit == XTT_SU_STORE;
  e.result_latency = get_attr_xtt_result_latency (insn) - 1;
  e.next_slot_stall
    = get_attr_xtt_next_slot_stall (insn) == XTT_NEXT_SLOT_STALL_YES;
  e.opaque = false;
  return e;
}

/* Post-admission operand access: the effect class (subunit load/store)
   has already admitted the instruction; reaching its typed operands by
   recognized code is the permitted use of code comparisons.  */

bool
rvtt_dst_access_operands (rtx_insn *insn, const xtt_effect_set &effects,
			  rtx *address, rtx *mode, rtx *addr_mode)
{
  if (effects.opaque || (!effects.dst_mem_read && !effects.dst_mem_write))
    return false;
  int code = recog_memoized (insn);
  int addr_pos, mode_pos, am_pos;
  if (code == CODE_FOR_rvtt_sfpload_lv_int)
    addr_pos = 4, mode_pos = 7, am_pos = 8;
  else if (code == CODE_FOR_rvtt_sfpstore_int)
    addr_pos = 3, mode_pos = 5, am_pos = 6;
  else
    return false;
  extract_insn (insn);
  *address = recog_data.operand[addr_pos];
  *mode = recog_data.operand[mode_pos];
  *addr_mode = recog_data.operand[am_pos];
  return true;
}

/* Gimple-level subunit query.  The table below is identity plumbing only:
   it names the late RTL pattern each audited builtin resolves to.  The
   effect data itself lives solely in the rvtt-cost.md attributes; the
   generated constant-attribute accessors switch on the memoized insn code
   without operand extraction, so a pattern-less scratch insn with a preset
   INSN_CODE is a legitimate query key.  Unlisted builtins return the
   refusing default.  */

struct builtin_late_code_map
{
  rvtt_insn_data::insn_id id;
  enum insn_code icode;
};

static const builtin_late_code_map builtin_late_codes[] = {
  { rvtt_insn_data::sfpmul,	    CODE_FOR_rvtt_sfpmul_lv },
  { rvtt_insn_data::sfpmul_lv,	    CODE_FOR_rvtt_sfpmul_lv },
  { rvtt_insn_data::sfpadd,	    CODE_FOR_rvtt_sfpadd_lv },
  { rvtt_insn_data::sfpadd_lv,	    CODE_FOR_rvtt_sfpadd_lv },
  { rvtt_insn_data::sfpmuli,	    CODE_FOR_rvtt_sfpmuli_int_lv },
  { rvtt_insn_data::sfpmuli_lv,	    CODE_FOR_rvtt_sfpmuli_int_lv },
  { rvtt_insn_data::sfpaddi,	    CODE_FOR_rvtt_sfpaddi_int_lv },
  { rvtt_insn_data::sfpaddi_lv,	    CODE_FOR_rvtt_sfpaddi_int_lv },
  { rvtt_insn_data::sfpmad,	    CODE_FOR_rvtt_sfpmad_lv },
  { rvtt_insn_data::sfpmad_lv,	    CODE_FOR_rvtt_sfpmad_lv },
  { rvtt_insn_data::sfpmul24,	    CODE_FOR_rvtt_sfpmul24_lv },
  { rvtt_insn_data::sfpmul24_lv,    CODE_FOR_rvtt_sfpmul24_lv },
  { rvtt_insn_data::sfpstochrnd_i,  CODE_FOR_rvtt_sfpstochrnd_i_lv_int },
  { rvtt_insn_data::sfpstochrnd_i_lv, CODE_FOR_rvtt_sfpstochrnd_i_lv_int },
  { rvtt_insn_data::sfpstochrnd_v,  CODE_FOR_rvtt_sfpstochrnd_v_lv },
  { rvtt_insn_data::sfpstochrnd_v_lv, CODE_FOR_rvtt_sfpstochrnd_v_lv },
  { rvtt_insn_data::sfpload,	    CODE_FOR_rvtt_sfpload_lv_int },
  { rvtt_insn_data::sfpstore,	    CODE_FOR_rvtt_sfpstore_int },
};

xtt_subunit_t
rvtt_builtin_subunit (const rvtt_insn_data *insnd)
{
  if (!insnd)
    return XTT_SU_NONE;
  for (const builtin_late_code_map &m : builtin_late_codes)
    if (m.id == insnd->id)
      {
	rtx_insn *scratch = as_a <rtx_insn *> (rtx_alloc (INSN));
	PATTERN (scratch) = const0_rtx;
	INSN_CODE (scratch) = (int) m.icode;
	return subunit_from_attr (get_attr_xtt_subunit (scratch));
      }
  return XTT_SU_NONE;
}

int
rvtt_builtin_result_latency (const rvtt_insn_data *insnd)
{
  if (!insnd)
    return -1;
  for (const builtin_late_code_map &m : builtin_late_codes)
    if (m.id == insnd->id)
      {
	/* Only patterns whose `xtt_result_latency' attribute is a
	   CONSTANT (operand-free) expression may be queried through a
	   pattern-less scratch insn; several patterns (sfpiadd, the
	   loads) condition the attribute on operand values and would
	   extract.  Allowlist = the MAD-family value ops, each carrying
	   the constant encoded latency "2" in rvtt.md.  Everything else
	   keeps the refusing default.  */
	if (m.icode != CODE_FOR_rvtt_sfpadd_lv
	    && m.icode != CODE_FOR_rvtt_sfpmul_lv
	    && m.icode != CODE_FOR_rvtt_sfpmad_lv
	    && m.icode != CODE_FOR_rvtt_sfpmul24_lv)
	  return -1;
	rtx_insn *scratch = as_a <rtx_insn *> (rtx_alloc (INSN));
	PATTERN (scratch) = const0_rtx;
	INSN_CODE (scratch) = (int) m.icode;
	/* Encoded latency+1; 0 = unaudited (FHS-5), hence the -1
	   refusing return after the bias is removed.  */
	return get_attr_xtt_result_latency (scratch) - 1;
      }
  return -1;
}

/* ---- Multi-result / shadow-coupled effect structure (TOP3-2 layer 1/2).

   Post-admission operand access, following the rvtt_dst_access_operands
   precedent: the effect attributes have already admitted the
   instruction; reaching its typed operands by recognized code is the
   permitted use of code comparisons.  The companion pairing itself is
   pattern data (the indexed SFPSWAP's register alternatives pin
   companion == value + 4; the eight-definition SFPTRANSP defines both
   banks), so the masks here are read off the extracted hard registers,
   never recomputed from folklore.  */

static uint32_t
hard_lreg_bit (rtx op)
{
  /* SFPU_REG_P, not `<= 16': see the mask-bound note above (FHN-5).  */
  if (REG_P (op) && HARD_REGISTER_P (op) && SFPU_REG_P (REGNO (op)))
    return 1u << (REGNO (op) - SFPU_REG_FIRST);
  return 0;
}

bool
rvtt_multiresult_group (rtx_insn *insn, const xtt_effect_set &effects,
			xtt_multiresult_group *group)
{
  if (effects.opaque)
    return false;
  int code = recog_memoized (insn);
  int value_ops, companion_ops;
  if (code == CODE_FOR_rvtt_sfpswap_indexed_int)
    value_ops = 2, companion_ops = 2;
  else if (code == CODE_FOR_rvtt_sfptransp8_int)
    value_ops = 4, companion_ops = 4;
  else
    return false;
  extract_insn (insn);
  group->value_write_mask = 0;
  group->companion_write_mask = 0;
  for (int i = 0; i != value_ops; ++i)
    group->value_write_mask |= hard_lreg_bit (recog_data.operand[i]);
  for (int i = 0; i != companion_ops; ++i)
    group->companion_write_mask
      |= hard_lreg_bit (recog_data.operand[value_ops + i]);
  return true;
}

/* Zero-length architectural LREG interface marker: a recognized Tensix
   pattern of no delivered words whose pattern mentions the variable-LREG
   unspec.  *LREG_MASK collects every hard LREG the marker pins.  */

static uint32_t
varlreg_reg_mask (const_rtx x)
{
  uint32_t mask = hard_lreg_bit (const_cast<rtx> (x));
  const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (int i = GET_RTX_LENGTH (GET_CODE (x)); i--;)
    if (fmt[i] == 'e')
      mask |= varlreg_reg_mask (XEXP (x, i));
    else if (fmt[i] == 'E')
      for (int j = XVECLEN (x, i); j--;)
	mask |= varlreg_reg_mask (XVECEXP (x, i, j));
  return mask;
}

static bool
mentions_varlreg_p (const_rtx x)
{
  if (GET_CODE (x) == UNSPEC_VOLATILE && XINT (x, 1) == UNSPECV_SFPVARLREG)
    return true;
  const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (int i = GET_RTX_LENGTH (GET_CODE (x)); i--;)
    if (fmt[i] == 'e')
      {
	if (mentions_varlreg_p (XEXP (x, i)))
	  return true;
      }
    else if (fmt[i] == 'E')
      for (int j = XVECLEN (x, i); j--;)
	if (mentions_varlreg_p (XVECEXP (x, i, j)))
	  return true;
  return false;
}

bool
rvtt_lreg_marker (rtx_insn *insn, uint32_t *lreg_mask)
{
  if (!insn || GET_CODE (insn) != INSN || recog_memoized (insn) < 0)
    return false;
  if (get_attr_type (insn) != TYPE_TENSIX || get_attr_length (insn))
    return false;
  if (!mentions_varlreg_p (PATTERN (insn)))
    return false;
  *lreg_mask = varlreg_reg_mask (PATTERN (insn));
  return true;
}

/* Recording-epoch closure proof.  See rvtt-effects.h for the contract.
   Word accounting uses the machine description's typed instruction
   lengths -- the delivered-word count of a recognized Tensix instruction
   is a typed fact independent of its effect audit -- while anything
   without a typed length (asm, calls, unrecognized insns) refuses by
   name.  No operation identity, opcode calendar, coefficient value, or
   instruction-word fingerprint participates.  */

xtt_replay_epoch
rvtt_replay_epoch_close (rtx_insn *capture, unsigned payload_words)
{
  xtt_replay_epoch epoch = { xtt_replay_epoch::CROSSES_BLOCK,
			     nullptr, nullptr, 0 };
  basic_block bb = BLOCK_FOR_INSN (capture);
  rtx_insn *stop = NEXT_INSN (BB_END (bb));
  unsigned remaining = payload_words;

  if (!remaining)
    {
      /* A zero-length capture records nothing; trivially closed.  */
      epoch.status = xtt_replay_epoch::CLOSED;
      epoch.close_at = capture;
      return epoch;
    }

  for (rtx_insn *cur = NEXT_INSN (capture); cur && cur != stop && remaining;
       cur = NEXT_INSN (cur))
    {
      if (!NONDEBUG_INSN_P (cur))
	continue;
      if (CALL_P (cur))
	{
	  epoch.status = xtt_replay_epoch::OPAQUE_PAYLOAD;
	  epoch.blocker = cur;
	  return epoch;
	}
      if (GET_CODE (cur) != INSN)
	/* A jump delivers no Tensix word; the block boundary itself is
	   handled by the loop bound.  */
	continue;
      rtx pattern = PATTERN (cur);
      if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER)
	continue;
      if (asm_noperands (pattern) >= 0 || recog_memoized (cur) < 0)
	{
	  /* Canonical raw `.ttinsn' constant words (the TTI_ macro shape
	     of the LLK library, rvtt-raw-boundary extraction) deliver
	     exactly one 32-bit instruction word each -- a typed-length
	     fact of the canonical shape itself, independent of the
	     word's effect audit.  The recording swallows payload words
	     without executing them, so the word COUNT is the whole
	     obligation here; the one exception is a raw word of the
	     architectural replay-owner opcode (the REPLAY encoding of
	     the target's encoding table), which stays refused so the
	     typed OWNER_DURING_CAPTURE refusal cannot be bypassed by
	     spelling the owner as a raw word.  Multi-word or otherwise
	     non-canonical asm keeps the refusing default.  */
	  uint32_t raw_word;
	  if (asm_noperands (pattern) >= 0
	      && rvtt_raw_ttinsn_word (cur, &raw_word)
	      && !rvtt_raw_replay_owner_word_p (raw_word))
	    {
	      remaining -= 1;
	      epoch.close_at = cur;
	      continue;
	    }
	  epoch.status = xtt_replay_epoch::OPAQUE_PAYLOAD;
	  epoch.blocker = cur;
	  return epoch;
	}
      if (get_attr_type (cur) != TYPE_TENSIX)
	/* Scalar RISC work pushes no Tensix word.  */
	continue;
      if (get_attr_xtt_replay (cur) == XTT_REPLAY_OWNER)
	{
	  epoch.status = xtt_replay_epoch::OWNER_DURING_CAPTURE;
	  epoch.blocker = cur;
	  return epoch;
	}
      unsigned words = get_attr_length (cur) / 4;
      if (!words)
	continue;
      if (words > remaining)
	{
	  /* The declared capture length would split this instruction's
	     words: broken user code; refuse.  */
	  epoch.status = xtt_replay_epoch::OPAQUE_PAYLOAD;
	  epoch.blocker = cur;
	  return epoch;
	}
      xtt_effect_set e = rvtt_insn_effects (cur);
      xtt_multiresult_group group;
      if (rvtt_multiresult_group (cur, e, &group))
	epoch.multiresult_members++;
      remaining -= words;
      epoch.close_at = cur;
    }

  if (remaining || !epoch.close_at)
    {
      epoch.status = xtt_replay_epoch::CROSSES_BLOCK;
      return epoch;
    }

  /* Extend the closure across immediately following zero-length LREG
     interface markers: they carry the payload's fixed-LREG protocol
     (companion-group integrity) and deliver no word.  */
  for (rtx_insn *cur = NEXT_INSN (epoch.close_at); cur && cur != stop;
       cur = NEXT_INSN (cur))
    {
      if (!NONDEBUG_INSN_P (cur))
	continue;
      uint32_t mask;
      if (!rvtt_lreg_marker (cur, &mask))
	break;
      epoch.close_at = cur;
    }

  epoch.status = xtt_replay_epoch::CLOSED;
  return epoch;
}

/* Function-sticky shadow-coupling possibility.  See rvtt-effects.h.  */

bool
rvtt_shadow_coupling_possible (function *fn)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn) || GET_CODE (insn) != INSN)
	    continue;
	  if (recog_memoized (insn) < 0)
	    continue;
	  if (get_attr_type (insn) != TYPE_TENSIX)
	    continue;
	  xtt_effect_set e = rvtt_insn_effects (insn);
	  if (e.opaque)
	    continue;
	  xtt_multiresult_group group;
	  if (rvtt_multiresult_group (insn, e, &group))
	    return true;
	  if (e.config_dests_written & (1u << 15))
	    return true;
	}
    }
  return false;
}

/* Debug/self-check annotation of the effect set, emitted as an assembler
   comment after each Tensix or asm instruction under
   -mtt-tensix-dump-effects.  DejaGnu golden tests pin these lines.  */

static const char *const subunit_names[] = {
  "none", "simple", "mad", "round", "load", "store", "cfg", "sync"
};
static const char *const port_names[] = {
  "none", "own", "shared_simple_round", "borrows_mad"
};

void
rvtt_dump_insn_effects (FILE *file, rtx_insn *insn)
{
  if (!insn || !NONDEBUG_INSN_P (insn))
    return;
  bool is_asm = asm_noperands (PATTERN (insn)) >= 0;
  if (!is_asm
      && (recog_memoized (insn) < 0 || get_attr_type (insn) != TYPE_TENSIX))
    return;

  xtt_effect_set e = rvtt_insn_effects (insn);
  if (e.opaque)
    {
      fprintf (file, "\t# xtt-effects: opaque\n");
      return;
    }

  fprintf (file, "\t# xtt-effects: subunit=%s latency=%d lreg-read=0x%x"
	   " lreg-write=0x%x port=%s cc=%s%s%s%s config=0x%x",
	   subunit_names[e.subunit], e.result_latency, e.lreg_read,
	   e.lreg_write, port_names[get_attr_xtt_lreg_write_port (insn)],
	   e.cc_read ? "r" : "", e.cc_write ? "w" : "",
	   /* Lane-state proof of the CC write: only a word-exact
	      all-lanes SFPENCC is proven; everything else dumps the
	      bare (unproved) "w".  */
	   e.cc_write_all_lanes ? ":all-lanes" : "",
	   !e.cc_read && !e.cc_write ? "none" : "",
	   e.config_dests_written);

  switch (e.rwc.kind)
    {
    case xtt_rwc_effect_t::NONE:
      fprintf (file, " rwc=none");
      break;
    case xtt_rwc_effect_t::INC:
      fprintf (file, " rwc=inc:d=%d,cr=%d", e.rwc.dst_delta, e.rwc.cr_delta);
      break;
    case xtt_rwc_effect_t::SET:
      fprintf (file, " rwc=set:mask=0x%x", e.rwc.set_mask);
      break;
    case xtt_rwc_effect_t::FACE:
      fprintf (file, " rwc=face:d=%d", e.rwc.dst_delta);
      break;
    case xtt_rwc_effect_t::UNKNOWN:
      fprintf (file, " rwc=unknown");
      break;
    }

  fprintf (file, " dst=%s%s%s encodable=%s\n",
	   e.dst_mem_read ? "r" : "", e.dst_mem_write ? "w" : "",
	   !e.dst_mem_read && !e.dst_mem_write ? "none" : "",
	   get_attr_xtt_macro_encodable (insn) == XTT_MACRO_ENCODABLE_YES
	   ? "yes" : "no");
}

/* ---- Planner emission records: launch issue-plane effects (see the
   contract block in rvtt-effects.h).  The store keeps no GC pointers:
   each record holds the emitted insn's UID, the emitting function's
   DECL_UID, the encoded launch word, and the launch VD hard register,
   and a lookup succeeds only when every one of them still matches the
   recognized SFPLOADMACRO pattern instance -- integrity verification of
   the planner's own emission, never region identity.  The planner pass
   resets the store at entry for every function it visits; functions the
   planner never visits can hold no records (the store starts empty and
   only the planner writes it).  */

struct planner_launch_record
{
  int uid;
  unsigned fn_uid;
  uint64_t word;
  unsigned vd_regno;
  xtt_effect_set fx;
};

static vec<planner_launch_record> planner_launch_records;

void
rvtt_planner_launch_effects_reset ()
{
  planner_launch_records.truncate (0);
}

void
rvtt_planner_launch_effects_record (rtx_insn *insn, uint64_t word,
				    unsigned vd_regno,
				    const xtt_effect_set &fx)
{
  gcc_assert (insn && cfun);
  planner_launch_record rec;
  rec.uid = INSN_UID (insn);
  rec.fn_uid = DECL_UID (cfun->decl);
  rec.word = word;
  rec.vd_regno = vd_regno;
  rec.fx = fx;
  planner_launch_records.safe_push (rec);
}

bool
rvtt_planner_launch_effects (rtx_insn *insn, xtt_effect_set *out)
{
  if (planner_launch_records.is_empty ()
      || !insn || !NONDEBUG_INSN_P (insn) || !cfun)
    return false;
  int code = recog_memoized (insn);
  if (code != CODE_FOR_rvtt_sfploadmacro_int
      && code != CODE_FOR_rvtt_sfploadmacro_hidden_int)
    return false;
  unsigned fn_uid = DECL_UID (cfun->decl);
  int uid = INSN_UID (insn);
  for (const planner_launch_record &rec : planner_launch_records)
    {
      if (rec.uid != uid || rec.fn_uid != fn_uid)
	continue;
      /* Fail-closed integrity: the recognized instance must still carry
	 the recorded launch word and VD.  */
      extract_insn (insn);
      if (recog_data.n_operands < 7
	  || !REG_P (recog_data.operand[0])
	  || !HARD_REGISTER_P (recog_data.operand[0])
	  || REGNO (recog_data.operand[0]) != rec.vd_regno
	  || !CONST_INT_P (recog_data.operand[6])
	  || UINTVAL (recog_data.operand[6]) != rec.word)
	return false;
      *out = rec.fx;
      return true;
    }
  return false;
}

/* ------------------------------------------------------------------ */
/* The audited-effect attribute migration.

   The two queries below are the ONLY decoders of the xtt_lane_local /
   xtt_cc_write / xtt_lane_gated attributes (rvtt-cost.md).  They
   replace the effect_overrides tables formerly copied verbatim between
   rtl-rvtt-lp-alloc.cc and rtl-rvtt-dst-ownership.cc and the hand
   lane_gated_consumers insn_code allowlist of rtl-rvtt-lp-alloc.cc.
   The one-pin equality-assertion phase (legacy tables recomputed under
   -fchecking) was discharged corpus- and testsuite-wide at pin 50 with
   zero inequalities and deleted at pin 51.  */

bool
rvtt_lane_local_effects (rtx_insn *insn, bool *cc_writes)
{
  int code = recog_memoized (insn);
  bool hit = false;
  bool ccw = false;
  if (code >= 0
      && get_attr_xtt_lane_local (insn) == XTT_LANE_LOCAL_YES)
    {
      hit = true;
      enum xtt_cc_write w = get_attr_xtt_cc_write (insn);
      /* Family invariant: an admitted lane-local row always carries an
	 audited CC_WRITES verdict (never the refusing unknown).  */
      gcc_assert (w != XTT_CC_WRITE_UNKNOWN);
      ccw = (w == XTT_CC_WRITE_YES);
    }
  if (hit)
    *cc_writes = ccw;
  return hit;
}

bool
rvtt_lane_gated_consumer_p (rtx_insn *insn)
{
  int code = recog_memoized (insn);
  bool hit = code >= 0
	     && get_attr_xtt_lane_gated (insn) == XTT_LANE_GATED_YES;
  return hit;
}
