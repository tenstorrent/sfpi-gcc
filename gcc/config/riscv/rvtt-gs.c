/* TT helper routines for GS
   Copyright (C) 2022 Free Software Foundation, Inc.
   Contributed by Paul Keller (pkeller@tenstorrent.com).

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

void rvtt_gs_emit_sfpload(rtx dst, rtx lv, rtx addr, rtx mod, rtx imm, rtx nonimm, rtx id)
{
  if (GET_CODE(imm) == CONST_INT) {
    emit_insn(gen_rvtt_gs_sfpload_int(dst, lv, mod, rvtt_clamp_unsigned(imm, 0xFFFF)));
  } else {
    unsigned long int op = TT_OP_GS_SFPLOAD(0, INTVAL(mod), 0);
    emit_insn(gen_rvtt_sfpnonimm_dst(dst, addr, GEN_INT(INSN_SCHED_DYN_DEP), lv, GEN_INT(20), nonimm, GEN_INT(op), id));
  }
}

void rvtt_gs_emit_sfpxloadi(rtx dst, rtx lv, rtx addr, rtx mod, rtx imm, rtx nonimm, rtx id)
{
  if (GET_CODE(imm) == CONST_INT) {
    emit_insn(gen_rvtt_gs_sfploadi_int(dst, lv, mod, rvtt_clamp_signed(imm, 0x7FFF)));
  } else {
    unsigned long int op = TT_OP_GS_SFPLOADI(0, INTVAL(mod), 0);
    emit_insn(gen_rvtt_sfpnonimm_dst(dst, addr, GEN_INT(0), lv, GEN_INT(20), nonimm, GEN_INT(op), id));
  }
}

void rvtt_gs_emit_sfpiadd_i(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod, rtx nonimm, rtx id)
{
  if (GET_CODE(imm) == CONST_INT) {
    emit_insn(gen_rvtt_gs_sfpiadd_i_int(dst, lv, src, rvtt_clamp_signed(imm, 0x7FF), mod));
  } else {
    unsigned int mod1 = UINTVAL(mod);
    unsigned long int op = TT_OP_GS_SFPIADD(0, 0, 0, mod1);
    int nnops = (mod1 < 3 || mod1 > 7) ? 3 : 2;
    emit_insn(gen_rvtt_sfpnonimm_dst_src(dst, addr, GEN_INT(nnops), lv, src, GEN_INT(4), GEN_INT(8), nonimm, GEN_INT(op), id));
  }
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
//   <=, > use multiple instructions, <= uses a COMPC which relies on the
//   wrapper emitting a PUSHC as a "fence" for the COMPC when needed
//
// Below, n is either not 0 or unknown
//   compare  < n or >= n  use iadd_i (subtract and compare)
//   compare == n or != n  use iadd_i and setcc (subtract then compare)
//
// Note: wrapper/instruction combining cannot create the case where the op
// is either <= n or > n and we care about the result.  The code below doesn't
// handle it and if it did, the result would be inefficient.
//
void rvtt_gs_emit_sfpxiadd_i(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod)
{
  unsigned int modi = INTVAL(mod);
  unsigned int cmp = modi & SFPXCMP_MOD1_CC_MASK;
  unsigned int base_mod = modi & ~SFPXCMP_MOD1_CC_MASK;

  // Decompose aggregate comparisons, recurse
  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT) {
    rvtt_gs_emit_sfpxiadd_i(dst, lv, addr, src, imm, GEN_INT(base_mod | SFPXCMP_MOD1_CC_GTE));
    rvtt_gs_emit_sfpxiadd_i(dst, lv, addr, dst, GEN_INT(0), GEN_INT(base_mod | SFPXCMP_MOD1_CC_NE));
    if (cmp == SFPXCMP_MOD1_CC_LTE) {
      emit_insn(gen_rvtt_gs_sfpcompc());
    }
    return;
  }

  bool need_loadi = true;
  bool is_signed = ((modi & SFPXIADD_MOD1_SIGNED) == SFPXIADD_MOD1_SIGNED);
  bool is_12bits = modi & SFPXIADD_MOD1_12BIT;
  bool is_const_int = GET_CODE(imm) == CONST_INT;
  bool is_sub = ((modi & SFPXIADD_MOD1_IS_SUB) != 0);
  bool dst_unused = ((modi & SFPXIADD_MOD1_DST_UNUSED) != 0);
  int iv = is_const_int ? INTVAL(imm) : 0xffffffff;

  // Figure out if we need to do a loadi (>12 bits signed)
  if (is_const_int) {
    int range_iv = is_sub ? -iv : iv;
    if (!(range_iv >= 2048 || range_iv < -2048)) {
      need_loadi = false;
      imm = GEN_INT(range_iv);
    }
  } else if (is_12bits) {
    // Future work
    // need_loadi = false;
    gcc_assert(0);
  }

  rtx set_cc_arg = src;

  bool need_setcc = ((cmp & SFPXCMP_MOD1_CC_MASK) != 0);
  if (need_loadi) {
    // Load imm into dst
    int loadi_mod = is_signed ? SFPLOADI_MOD0_SHORT : SFPLOADI_MOD0_USHORT;
    if (is_const_int) {
      if (is_signed) {
	if (iv >= 32768) {
	  iv &= 0x7fff;
	} else if (iv < -32768) {
	  iv |= ~0x7fff;
	}
      } else if (iv >= 65536) {
	iv &= 0xffff;
      }
      imm = GEN_INT(iv);
    }
    rvtt_gs_emit_sfpxloadi(dst, rvtt_gen_const0_vector(), addr, GEN_INT(loadi_mod), imm, GEN_INT(0), GEN_INT(0));

    unsigned int mod1 = is_sub ? SFPIADD_MOD1_ARG_2SCOMP_LREG_DST : SFPIADD_MOD1_ARG_LREG_DST;
    if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GTE) {
      // Perform op w/ compare
      mod1 |= (cmp == SFPXCMP_MOD1_CC_LT) ? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
      emit_insn(gen_rvtt_gs_sfpiadd_v_int(dst, dst, src, GEN_INT(mod1)));
      need_setcc = false;
    } else {
      // Perform op w/o compare, compare with SETCC
      mod1 |= SFPIADD_MOD1_CC_NONE;
      emit_insn(gen_rvtt_gs_sfpiadd_v_int(dst, dst, src, GEN_INT(mod1)));
      set_cc_arg = dst;
    }
  } else if (is_const_int) {
    if (iv != 0) {
      if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GTE) {
	// Perform op w/ compare
	unsigned int mod1 = (cmp == SFPXCMP_MOD1_CC_LT) ? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
	emit_insn(gen_rvtt_gs_sfpiadd_i_int(dst, lv, src, imm, GEN_INT(mod1 | SFPIADD_MOD1_ARG_IMM)));
	need_setcc = false;
      } else {
	// Perform op w/o compare
	emit_insn(gen_rvtt_gs_sfpiadd_i_int(dst, lv, src, imm,
					    GEN_INT(SFPIADD_MOD1_ARG_IMM | SFPIADD_MOD1_CC_NONE)));
	set_cc_arg = dst;
      }
    } else if (!dst_unused) {
      // An add or subtract against 0 isn't particularly interesting, but
      // we need to keep the register usage correct since dst is now src
      emit_insn(gen_rvtt_gs_sfpiadd_i_int(dst, lv, src, imm,
					  GEN_INT(SFPIADD_MOD1_ARG_IMM | SFPIADD_MOD1_CC_NONE)));
    }
  } else {
    // This code path could handle the case where the operand isn't a CONST_INT (so
    // the value isn't known at compile time) but some (future) mechanism
    // (wrapper API or pragma) ensures that the resulting value fits in 12
    // bits and so an IADDI can be used.
    gcc_assert(is_12bits);
    gcc_assert(false);
  }

  if (need_setcc) {
    emit_insn(gen_rvtt_gs_sfpsetcc_v(set_cc_arg, GEN_INT(rvtt_cmp_ex_to_setcc_mod1_map[cmp])));
  }
}

// See comment block above sfpiadd_i_ex
void rvtt_gs_emit_sfpxiadd_v(rtx dst, rtx srcb, rtx srca, rtx mod)
{
  unsigned int modi = INTVAL(mod);
  unsigned int cmp = modi & SFPXCMP_MOD1_CC_MASK;
  unsigned int base_mod = modi & ~SFPXCMP_MOD1_CC_MASK;

  // Decompose aggregate comparisons, recurse
  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT) {
    rvtt_gs_emit_sfpxiadd_v(dst, srcb, srca, GEN_INT(base_mod | SFPXCMP_MOD1_CC_GTE));
    emit_insn(gen_rvtt_gs_sfpsetcc_v(dst, GEN_INT(SFPSETCC_MOD1_LREG_NE0)));
    if (cmp == SFPXCMP_MOD1_CC_LTE) {
      emit_insn(gen_rvtt_gs_sfpcompc());
    }
    return;
  }

  bool is_sub = ((modi & SFPXIADD_MOD1_IS_SUB) != 0);
  unsigned int mod1 = is_sub ? SFPIADD_MOD1_ARG_2SCOMP_LREG_DST : SFPIADD_MOD1_ARG_LREG_DST;

  if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GTE) {
    // Perform op w/ compare
    mod1 |= (cmp == SFPXCMP_MOD1_CC_LT) ? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
    emit_insn(gen_rvtt_gs_sfpiadd_v_int(dst, srcb, srca, GEN_INT(mod1)));
  } else {
    // Perform op w/o compare
    mod1 |= SFPIADD_MOD1_CC_NONE;
    emit_insn(gen_rvtt_gs_sfpiadd_v_int(dst, srcb, srca, GEN_INT(mod1)));
    if (cmp != 0) {
      // Must be EQ0 or NE0, compare with SETCC
      emit_insn(gen_rvtt_gs_sfpsetcc_v(dst, GEN_INT(rvtt_cmp_ex_to_setcc_mod1_map[cmp])));
    }
  }
}

void rvtt_gs_emit_sfpxfcmps(rtx addr, rtx v, rtx f, rtx mod)
{
  bool need_sub = false;
  rtx ref_val = gen_reg_rtx(V64SFmode);

  gcc_assert(GET_CODE(f) == CONST_INT);
  int fval = INTVAL(f);
  // Wrapper will convert 0 to -0
  if (fval != 0 && fval != 0x8000) {
    need_sub = true;

    switch (fval) {
      // Could add more CReg values here, doubt they show up in cmp
    case 0x3f80:
      rvtt_emit_sfpassignlreg(ref_val, GEN_INT(CREG_IDX_1));
      break;
    case 0xbf00:
      rvtt_emit_sfpassignlreg(ref_val, GEN_INT(CREG_IDX_NEG_0P5));
      break;
    case 0xbf80:
      rvtt_emit_sfpassignlreg(ref_val, GEN_INT(CREG_IDX_NEG_1));
      break;
    default:
      int loadi_mod = ((INTVAL(mod) & SFPXSCMP_MOD1_FMT_A) == SFPXSCMP_MOD1_FMT_A) ?
	SFPLOADI_MOD0_FLOATA : SFPLOADI_MOD0_FLOATB;
      rvtt_gs_emit_sfpxloadi(ref_val, rvtt_gen_const0_vector(), addr, GEN_INT(loadi_mod), f,
			     GEN_INT(0), GEN_INT(0));
      break;
    }
  }

  unsigned int cmp = INTVAL(mod) & SFPXCMP_MOD1_CC_MASK;
  rtx setcc_mod = GEN_INT(rvtt_cmp_ex_to_setcc_mod1_map[cmp]);
  if (need_sub) {
    rtx neg_one = gen_reg_rtx(V64SFmode);
    rtx tmp = gen_reg_rtx(V64SFmode);
    rvtt_emit_sfpassignlreg(neg_one, GEN_INT(CREG_IDX_NEG_1));
    emit_insn(gen_rvtt_gs_sfpmad(tmp, ref_val, neg_one, v, GEN_INT(0)));
    v = tmp;
  }

  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT) {
    emit_insn(gen_rvtt_gs_sfpsetcc_v(v, GEN_INT(SFPSETCC_MOD1_LREG_GTE0)));
    emit_insn(gen_rvtt_gs_sfpsetcc_v(v, GEN_INT(SFPSETCC_MOD1_LREG_NE0)));
    if (cmp == SFPXCMP_MOD1_CC_LTE) {
      emit_insn(gen_rvtt_gs_sfpcompc());
    }
  } else {
    emit_insn(gen_rvtt_gs_sfpsetcc_v(v, setcc_mod));
  }
}

// Compare two vectors by subtracting v2 from v1 and doing a setcc
void rvtt_gs_emit_sfpxfcmpv(rtx v1, rtx v2, rtx mod)
{
  rtx tmp = gen_reg_rtx(V64SFmode);
  rtx neg1 = gen_reg_rtx(V64SFmode);

  rvtt_emit_sfpassignlreg(neg1, GEN_INT(CREG_IDX_NEG_1));
  emit_insn(gen_rvtt_gs_sfpmad(tmp, v2, neg1, v1, GEN_INT(0)));

  unsigned int cmp = INTVAL(mod) & SFPXCMP_MOD1_CC_MASK;
  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT) {
    emit_insn(gen_rvtt_gs_sfpsetcc_v(tmp, GEN_INT(SFPSETCC_MOD1_LREG_GTE0)));
    emit_insn(gen_rvtt_gs_sfpsetcc_v(tmp, GEN_INT(SFPSETCC_MOD1_LREG_NE0)));
    if (cmp == SFPXCMP_MOD1_CC_LTE) {
      emit_insn(gen_rvtt_gs_sfpcompc());
    }
  } else {
    emit_insn(gen_rvtt_gs_sfpsetcc_v(tmp, GEN_INT(rvtt_cmp_ex_to_setcc_mod1_map[INTVAL(mod)])));
  }
}

void rvtt_gs_emit_sfpdivp2(rtx dst, rtx lv, rtx addr, rtx imm, rtx src, rtx mod, rtx nonimm, rtx id)
{
  if (GET_CODE(imm) == CONST_INT) {
    emit_insn(gen_rvtt_gs_sfpdivp2_int(dst, lv, rvtt_clamp_signed(imm, 0x7FF), src, mod));
  } else {
    unsigned long int op = TT_OP_GS_SFPDIVP2(0, 0, 0, INTVAL(mod));
    emit_insn(gen_rvtt_sfpnonimm_dst_src(dst, addr, GEN_INT(2), lv, src, GEN_INT(4), GEN_INT(8), nonimm, GEN_INT(op), id));
  }
}
