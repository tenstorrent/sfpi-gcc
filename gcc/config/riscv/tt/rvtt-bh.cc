/* TT helper routines for GS
   Copyright (C) 2022-2025 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).

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
#define INCLUDE_STRING
#include <map>
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "regs.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "recog.h"
#include "output.h"
#include "alias.h"
#include "stringpool.h"
#include "attribs.h"
#include "varasm.h"
#include "stor-layout.h"
#include "calls.h"
#include "function.h"
#include "explow.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "reload.h"
#include "tm_p.h"
#include "target.h"
#include "target-def.h"
#include "basic-block.h"
#include "expr.h"
#include "optabs.h"
#include "bitmap.h"
#include "df.h"
#include "diagnostic.h"
#include "builtins.h"
#include "predict.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "rvtt-protos.h"
#include "rvtt.h"

void rvtt_emit_sfpload_bh(rtx dst, rtx lv, rtx addr, rtx mod, rtx mode, rtx imm, rtx nonimm, rtx id)
{
  rtx insn = nullptr; 
  if (CONST_INT_P (imm))
    insn = gen_rvtt_sfpload_int_bh (dst, lv, mod, mode, rvtt_clamp_unsigned (imm, 0x3FFF));
  else
    {
      unsigned op = TT_OP_BH_SFPLOAD (0, INTVAL (mod), INTVAL (mode), 0);
      insn = rvtt_sfpsynth_insn_dst (addr, CODE_FOR_rvtt_sfpload_int_bh,
				     0, nonimm, op, id, dst, 20, lv);
    }
  emit_insn (insn);
}

void rvtt_emit_sfpxloadi_bh(rtx dst, rtx lv, rtx addr, rtx mod, rtx imm, rtx nonimm, rtx id)
{
  int int_mod = INTVAL(mod);

  if (int_mod & SFPXLOADI_MOD0_32BIT_MASK) {
    // Early nonimm pass assures this
    gcc_assert(CONST_INT_P (imm));

    unsigned int int_imm = INTVAL(imm);
    bool load_32bit = true;
    unsigned int new_mod;

    switch (int_mod) {
    case SFPXLOADI_MOD0_INT32:
      // This gets interesting since we can do a signed load of a 16 bit
      // positive integer by using an unsigned load to fill the upper bits
      // with 0s
      if (!(static_cast<int>(int_imm) > 32767 || static_cast<int>(int_imm) < -32768)) {
	new_mod = SFPLOADI_MOD0_SHORT;
	load_32bit = false;
      } else if (static_cast<int>(int_imm) >= 0 && static_cast<int>(int_imm) < 65536) {
	new_mod = SFPLOADI_MOD0_USHORT;
	load_32bit = false;
      }
      break;

    case SFPXLOADI_MOD0_UINT32:
      if (int_imm <= 0xFFFF) {
	new_mod = SFPLOADI_MOD0_USHORT;
	load_32bit = false;
      }
      break;

    case SFPXLOADI_MOD0_FLOAT:
      {
	unsigned int man = int_imm & 0x007FFFFF;
	int exp = ((int_imm >> 23) & 0xFF) - 127;

	if ((man & 0xFFFF) == 0) {
	  // Fits in fp16b
	  load_32bit = false;
	  new_mod = SFPLOADI_MOD0_FLOATB;
	  int_imm = rvtt_fp32_to_fp16b(int_imm);
	} else if ((man & 0x1FFF) == 0 && exp < 16 && exp >= -14) {
	  // Fits in fp16a
	  load_32bit = false;
	  new_mod = SFPLOADI_MOD0_FLOATA;
	  int_imm = rvtt_fp32_to_fp16a(int_imm);
	}

	imm = GEN_INT(int_imm);
      }
      break;

    default:
      gcc_assert(0);
    }
    if (load_32bit) {
      emit_insn(gen_rvtt_sfploadi_int_bh(dst, lv, GEN_INT(SFPLOADI_MOD0_UPPER), GEN_INT(int_imm >> 16)));
      emit_insn(gen_rvtt_sfploadi_int_bh(dst, dst, GEN_INT(SFPLOADI_MOD0_LOWER), GEN_INT(int_imm & 0xFFFF)));
    } else {
      emit_insn(gen_rvtt_sfploadi_int_bh(dst, lv, GEN_INT(new_mod), imm));
    }
  } else {
    rtx insn = nullptr;
    if (CONST_INT_P (imm))
      insn = gen_rvtt_sfploadi_int_bh (dst, lv, GEN_INT(int_mod), rvtt_clamp_signed(imm, 0x7FFF));
    else
      {
	unsigned long int op = TT_OP_BH_SFPLOADI(0, int_mod, 0);
	insn = rvtt_sfpsynth_insn_dst (addr, CODE_FOR_rvtt_sfploadi_int_bh,
				       0, nonimm, op, id, dst, 20, lv);
      }
    emit_insn (insn);
  }
}

void rvtt_emit_sfpiadd_i_bh(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod, rtx nonimm, rtx id)
{
  rtx insn = nullptr;
  if (CONST_INT_P (imm))
    insn = gen_rvtt_sfpiadd_i_int_bh (dst, lv, src, rvtt_clamp_signed (imm, 0x7FF), mod);
  else
    {
      unsigned op = TT_OP_BH_SFPIADD(0, 0, 0, UINTVAL (mod));
      insn = rvtt_sfpsynth_insn_dst (addr, CODE_FOR_rvtt_sfpiadd_i_int_bh,
				     0, nonimm, op, id, src, 4, dst, 8, lv);
    }
  emit_insn (insn);
}

// Extended (or external?) iadd_i
// Handles:
//   - signed/unsigned immediate value
//   - >12 bits (>11 bits for unsigned)
//   - comparators: <, ==, !=, >=, <=, >
//   - use of SETCC vs IADD for perf
//
// For comparisons:
//   compare  < 0 or >= 0  use setcc
//   compare == 0 or != 0  use setcc
//
//   <=, > use multiple instructions, <= uses a COMPC bhich relies on the
//   wrapper emitting a PUSHC as a "fence" for the COMPC bhen needed
//
// Below, n is either not 0 or unknown
//   compare  < n or >= n  use iadd_i (subtract and compare)
//   compare == n or != n  use iadd_i and setcc (subtract then compare)
//
// Note: wrapper/instruction combining cannot create the case bhere the op
// is either <= n or > n and we care about the result.  The code below doesn't
// handle it and if it did, the result would be inefficient.
//
void
rvtt_emit_sfpxiadd_i_bh (rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod, bool dst_used)
{
  unsigned int modi = INTVAL (mod);
  unsigned int cmp = modi & SFPXCMP_MOD1_CC_MASK;
  unsigned int base_mod = modi & ~SFPXCMP_MOD1_CC_MASK;

  // Decompose aggregate comparisons, recurse
  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT)
    {
      rtx tmp = gen_reg_rtx (XTT32SImode);
      rvtt_emit_sfpxiadd_i_bh (tmp, lv, addr, src, imm, GEN_INT (base_mod | SFPXCMP_MOD1_CC_GTE), true);
      rvtt_emit_sfpxiadd_i_bh (dst, lv, addr, tmp, const0_rtx, GEN_INT (base_mod | SFPXCMP_MOD1_CC_NE));
      if (cmp == SFPXCMP_MOD1_CC_LTE)
	emit_insn (gen_rvtt_sfpcompc ());
      return;
    }

  bool need_loadi = true;
  bool is_signed = (modi & SFPXIADD_MOD1_SIGNED) == SFPXIADD_MOD1_SIGNED;
  bool is_12bits = modi & SFPXIADD_MOD1_12BIT;
  bool is_const_int = CONST_INT_P (imm);
  bool is_sub = bool (modi & SFPXIADD_MOD1_IS_SUB);
  int iv = is_const_int ? INTVAL (imm) : 0xffffffff;

  // Figure out if we need to do a loadi (>12 bits signed)
  if (is_const_int)
    {
      iv = is_sub ? -iv : iv;
      if (!(iv >= 2048 || iv < -2048))
	{
	  need_loadi = false;
	  imm = GEN_INT(iv);
	}
    }
  else if (is_12bits)
    // Future work
    //need_loadi = false;
    gcc_unreachable ();

  rtx set_cc_arg = src;

  bool need_setcc = bool (cmp & SFPXCMP_MOD1_CC_MASK);
  if (need_loadi)
    {
      // Load imm into dst
      int loadi_mod = is_signed ? SFPXLOADI_MOD0_INT32 : SFPXLOADI_MOD0_UINT32;
      rvtt_emit_sfpxloadi_bh (dst, rvtt_gen_rtx_noval (XTT32SImode), addr,
			      GEN_INT(loadi_mod), imm, const0_rtx, const0_rtx);
      
      unsigned int mod1 = is_sub ? SFPIADD_MOD1_ARG_2SCOMP_LREG_DST : SFPIADD_MOD1_ARG_LREG_DST;
      if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GTE)
	{
	  // Perform op w/ compare
	  mod1 |= (cmp == SFPXCMP_MOD1_CC_LT) ? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
	  emit_insn (gen_rvtt_sfpiadd_v_int_bh(dst, dst, src, GEN_INT (mod1)));
	  need_setcc = false;
	}
      else
	{
	  // Perform op w/o compare, compare with SETCC
	  mod1 |= SFPIADD_MOD1_CC_NONE;
	  emit_insn(gen_rvtt_sfpiadd_v_int_bh(dst, dst, src, GEN_INT(mod1)));
	  set_cc_arg = dst;
	}
    }
  else if (is_const_int)
    {
      if (iv != 0)
	{
	  if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GTE)
	    {
	      // Perform op w/ compare
	      unsigned int mod1 = (cmp == SFPXCMP_MOD1_CC_LT) ? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
	      emit_insn(gen_rvtt_sfpiadd_i_int_bh(dst, lv, src, imm, GEN_INT(mod1 | SFPIADD_MOD1_ARG_IMM)));
	      need_setcc = false;
	    }
	  else
	    {
	      // Perform op w/o compare
	      emit_insn(gen_rvtt_sfpiadd_i_int_bh(dst, lv, src, imm,
						  GEN_INT(SFPIADD_MOD1_ARG_IMM | SFPIADD_MOD1_CC_NONE)));
	      set_cc_arg = dst;
	    }
	}
      else if (dst_used || !(modi & SFPXIADD_MOD1_DST_UNUSED))
	{
	  if (REG_P (lv))
	    emit_insn (gen_rvtt_sfpassign_lv (dst, lv, src));
	  else
	    emit_move_insn (dst, src);
	}
    }
  else
    {
      // This code path could handle the case bhere the operand isn't a CONST_INT (so
      // the value isn't known at compile time) but some (future) mechanism
      // (wrapper API or pragma) ensures that the resulting value fits in 12
      // bits and so an IADDI can be used.
      gcc_assert (is_12bits);
      gcc_unreachable ();
    }

  if (need_setcc)
    emit_insn (gen_rvtt_sfpsetcc_v (set_cc_arg, GEN_INT(rvtt_cmp_ex_to_setcc_mod1_map[cmp])));
}

// See comment block above sfpiadd_i_ex
void rvtt_emit_sfpxiadd_v_bh(rtx dst, rtx srcb, rtx srca, rtx mod)
{
  unsigned int modi = INTVAL(mod);
  unsigned int cmp = modi & SFPXCMP_MOD1_CC_MASK;
  unsigned int base_mod = modi & ~SFPXCMP_MOD1_CC_MASK;

  // Decompose aggregate comparisons, recurse
  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT) {
    rvtt_emit_sfpxiadd_v_bh(dst, srcb, srca, GEN_INT(base_mod | SFPXCMP_MOD1_CC_GTE));
    emit_insn(gen_rvtt_sfpsetcc_v(dst, GEN_INT(SFPSETCC_MOD1_LREG_NE0)));
    if (cmp == SFPXCMP_MOD1_CC_LTE)
      emit_insn(gen_rvtt_sfpcompc());
    return;
  }

  bool is_sub = ((modi & SFPXIADD_MOD1_IS_SUB) != 0);
  unsigned int mod1 = is_sub ? SFPIADD_MOD1_ARG_2SCOMP_LREG_DST : SFPIADD_MOD1_ARG_LREG_DST;

  if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GTE) {
    // Perform op w/ compare
    mod1 |= (cmp == SFPXCMP_MOD1_CC_LT) ? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
    emit_insn(gen_rvtt_sfpiadd_v_int_bh(dst, srcb, srca, GEN_INT(mod1)));
  } else {
    // Perform op w/o compare
    mod1 |= SFPIADD_MOD1_CC_NONE;
    emit_insn(gen_rvtt_sfpiadd_v_int_bh(dst, srcb, srca, GEN_INT(mod1)));
    if (cmp != 0)
      // Must be EQ0 or NE0, compare with SETCC
      emit_insn(gen_rvtt_sfpsetcc_v(dst, GEN_INT(rvtt_cmp_ex_to_setcc_mod1_map[cmp])));
  }
}

void rvtt_emit_sfpxfcmps_bh(rtx addr, rtx v, rtx f, rtx mod)
{
  bool need_sub = false;
  rtx ref_val = gen_reg_rtx(XTT32SImode);
  int int_mod = INTVAL(mod);

  gcc_assert(CONST_INT_P (f));
  unsigned int fval = INTVAL(f);
  // Wrapper will convert 0 to -0
  unsigned int fmt = int_mod & SFPXSCMP_MOD1_FMT_MASK;
  if (fval != 0 &&
      ((fmt != SFPXSCMP_MOD1_FMT_FLOAT && fval != 0x80000000) ||
       (fmt == SFPXSCMP_MOD1_FMT_FLOAT && fval != 0x8000)))
    {
      need_sub = true;
      if ((fmt == SFPXSCMP_MOD1_FMT_FLOAT && fval == 0x3f800000)
	  || (fmt != SFPXSCMP_MOD1_FMT_FLOAT && fval == 0x3f80))
	ref_val = rvtt_gen_rtx_creg (XTT32SImode, CREG_IDX_1);
      else if ((fmt == SFPXSCMP_MOD1_FMT_FLOAT && fval == 0xbf800000) ||
		 (fmt != SFPXSCMP_MOD1_FMT_FLOAT && fval == 0xbf80))
	ref_val = rvtt_gen_rtx_creg (XTT32SImode, CREG_IDX_NEG_1);
      else
	rvtt_emit_sfpxloadi_bh(ref_val, rvtt_gen_rtx_noval (XTT32SImode), addr,
			       GEN_INT(rvtt_scmp2loadi_mod(fmt)), f,
			       GEN_INT(0), GEN_INT(0));
    }

  unsigned int cmp = INTVAL(mod) & SFPXCMP_MOD1_CC_MASK;
  rtx setcc_mod = GEN_INT(rvtt_cmp_ex_to_setcc_mod1_map[cmp]);
  if (need_sub) {
    rtx tmp = gen_reg_rtx(XTT32SImode);
    rtx neg_one = rvtt_gen_rtx_creg (XTT32SImode, CREG_IDX_NEG_1);

    emit_insn(gen_rvtt_sfpmad_bh(tmp, ref_val, neg_one, v, GEN_INT(0)));
    v = tmp;
  }

  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT) {
    emit_insn(gen_rvtt_sfpsetcc_v(v, GEN_INT(SFPSETCC_MOD1_LREG_GTE0)));
    emit_insn(gen_rvtt_sfpsetcc_v(v, GEN_INT(SFPSETCC_MOD1_LREG_NE0)));
    if (cmp == SFPXCMP_MOD1_CC_LTE)
      emit_insn(gen_rvtt_sfpcompc());
  } else {
    emit_insn(gen_rvtt_sfpsetcc_v(v, setcc_mod));
  }
}

// Compare two vectors by subtracting v2 from v1 and doing a setcc
void rvtt_emit_sfpxfcmpv_bh(rtx v1, rtx v2, rtx mod)
{
  rtx tmp = gen_reg_rtx(XTT32SImode);
  rtx neg1 = rvtt_gen_rtx_creg (XTT32SImode, CREG_IDX_NEG_1);

  emit_insn(gen_rvtt_sfpmad_bh(tmp, v2, neg1, v1, GEN_INT(0)));

  unsigned int cmp = INTVAL(mod) & SFPXCMP_MOD1_CC_MASK;
  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT) {
    emit_insn(gen_rvtt_sfpsetcc_v(tmp, GEN_INT(SFPSETCC_MOD1_LREG_GTE0)));
    emit_insn(gen_rvtt_sfpsetcc_v(tmp, GEN_INT(SFPSETCC_MOD1_LREG_NE0)));
    if (cmp == SFPXCMP_MOD1_CC_LTE)
      emit_insn(gen_rvtt_sfpcompc());
  } else {
    emit_insn(gen_rvtt_sfpsetcc_v(tmp, GEN_INT(rvtt_cmp_ex_to_setcc_mod1_map[INTVAL(mod)])));
  }
}

void rvtt_emit_sfpdivp2_bh(rtx dst, rtx lv, rtx addr, rtx imm, rtx src, rtx mod, rtx nonimm, rtx id)
{
  rtx insn = nullptr;
  if (CONST_INT_P (imm))
    insn = gen_rvtt_sfpdivp2_int_bh (dst, lv, rvtt_clamp_signed (imm, 0x7FF), src, mod);
  else
    {
      unsigned op = TT_OP_BH_SFPDIVP2 (0, 0, 0, INTVAL (mod));
      insn = rvtt_sfpsynth_insn_dst (addr, CODE_FOR_rvtt_sfpdivp2_int_bh,
				     0, nonimm, op, id, src, 4, dst, 8, lv);
    }
  emit_insn (insn);
}

void rvtt_emit_sfpstochrnd_i_bh(rtx dst, rtx lv, rtx addr, rtx mode, rtx imm, rtx src, rtx mod, rtx nonimm, rtx id)
{
  rtx insn = nullptr;
  if (CONST_INT_P (imm))
    insn = gen_rvtt_sfpstochrnd_i_int_bh (dst, lv, mode, rvtt_clamp_unsigned(imm, 0x1F), src, mod);
  else
    {
      unsigned op = TT_OP_BH_SFP_STOCH_RND (INTVAL (mode), 0, 0, 0, 0, INTVAL (mod));
      insn = rvtt_sfpsynth_insn_dst (addr, CODE_FOR_rvtt_sfpstochrnd_i_int_bh,
				     0, nonimm, op, id, src, 4, dst, 8, lv);
    }
  emit_insn (insn);
}

void rvtt_emit_sfpsetman_bh(rtx dst, rtx lv, rtx addr, rtx imm, rtx src)
{
  if (CONST_INT_P (imm))
    {
      unsigned int iv = INTVAL(imm);
      if (iv > 4095) {
	rvtt_emit_sfpxloadi_bh(dst, lv, addr,
			       GEN_INT(SFPXLOADI_MOD0_UINT32), imm, GEN_INT(0), GEN_INT(0));
	emit_insn(gen_rvtt_sfpsetman_v_bh(dst, dst, src));
      } else
	emit_insn (gen_rvtt_sfpsetman_i_int_bh(dst, lv, imm, src));
    }
  else
    gcc_unreachable ();
}
