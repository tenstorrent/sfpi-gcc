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

/* Effect data lives once, in the generated attribute family of
   rvtt-cost.md; this file only resolves it against operands.  Every
   default is refusing: an unaudited pattern, a call, or any asm yields
   opaque=true and consumers must refuse byte-identically.  */

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
	if (REG_P (op) && HARD_REGISTER_P (op)
	    && REGNO (op) >= SFPU_REG_FIRST
	    && REGNO (op) - SFPU_REG_FIRST <= 16)
	  mask |= 1u << (REGNO (op) - SFPU_REG_FIRST);
      }
  return mask;
}

/* The canonical no-increment load/store address mode.  This constant is
   target capability data; it migrates into the WP6 capability tables and
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

xtt_effect_set
rvtt_insn_effects (rtx_insn *insn)
{
  xtt_effect_set e = {};
  e.opaque = true;
  e.result_latency = -1;
  e.rwc.kind = xtt_rwc_effect_t::UNKNOWN;

  if (!insn || !NONDEBUG_INSN_P (insn) || CALL_P (insn))
    return e;
  if (asm_noperands (PATTERN (insn)) >= 0)
    return e;
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
      || rwc == XTT_RWC_EFFECT_ADDR_MODE)
    extract_insn (insn);

  e.lreg_read = lreg_mask_of_positions (read_positions);
  e.lreg_write = lreg_mask_of_positions (write_positions);

  e.cc_read = (cc == XTT_CC_EFFECT_READ || cc == XTT_CC_EFFECT_READWRITE);
  e.cc_write = (cc == XTT_CC_EFFECT_WRITE || cc == XTT_CC_EFFECT_READWRITE);

  if (cfg == XTT_CONFIG_EFFECT_DEST)
    {
      int pos = get_attr_xtt_config_dest_op (insn) - 1;
      if (pos < 0 || pos >= recog_data.n_operands
	  || !CONST_INT_P (recog_data.operand[pos])
	  || INTVAL (recog_data.operand[pos]) < 0
	  || INTVAL (recog_data.operand[pos]) > 15)
	return e;		/* Non-constant config dest: refuse.  */
      e.config_dests_written = 1u << INTVAL (recog_data.operand[pos]);
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
	  /* Auto-increment deltas are capability-table data (WP6);
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
  e.opaque = false;
  return e;
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
	   " lreg-write=0x%x port=%s cc=%s%s%s config=0x%x",
	   subunit_names[e.subunit], e.result_latency, e.lreg_read,
	   e.lreg_write, port_names[get_attr_xtt_lreg_write_port (insn)],
	   e.cc_read ? "r" : "", e.cc_write ? "w" : "",
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
