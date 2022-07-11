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
#include "sfpu-protos.h"
#include "sfpu.h"

void riscv_sfpu_wh_emit_sfpload(rtx dst, rtx lv, rtx addr, rtx mod, rtx mode, rtx imm)
{
  if (GET_CODE(imm) == CONST_INT) {
    emit_insn(gen_riscv_wh_sfpload_int(dst, lv, mod, mode, riscv_sfpu_clamp_unsigned(imm, 0x3FFF)));
  } else {
    int base = TT_OP_WH_SFPLOAD(0, INTVAL(mod), INTVAL(mode), 0);
    riscv_sfpu_emit_nonimm_dst(addr, dst, 0, lv, imm, base, 16, 16, 20);
  }
}

void riscv_sfpu_wh_emit_sfpxloadi(rtx dst, rtx lv, rtx addr, rtx mod, rtx imm)
{
  int int_mod = INTVAL(mod);

  if (int_mod & SFPXLOADI_MOD0_32BIT_MASK) {
    // Wormhole only (mask above ensures this)
    if (GET_CODE(imm) == CONST_INT) {
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
	   int_imm = riscv_sfpu_fp32_to_fp16b(int_imm);
	 } else if ((man & 0x1FFF) == 0 && exp < 16 && exp >= -14) {
	   // Fits in fp16a
	   load_32bit = false;
	   new_mod = SFPLOADI_MOD0_FLOATA;
	   int_imm = riscv_sfpu_fp32_to_fp16a(int_imm);
	 }

	 imm = GEN_INT(int_imm);
       }
       break;

      default:
       gcc_assert(0);
      }
      if (load_32bit) {
       emit_insn(gen_riscv_wh_sfploadi_int(dst, lv, GEN_INT(SFPLOADI_MOD0_UPPER), GEN_INT(int_imm >> 16)));
       emit_insn(gen_riscv_wh_sfploadi_int(dst, dst, GEN_INT(SFPLOADI_MOD0_LOWER), GEN_INT(int_imm & 0xFFFF)));
      } else {
       emit_insn(gen_riscv_wh_sfploadi_int(dst, lv, GEN_INT(new_mod), imm));
      }
    } else {
      rtx riscv_tmp = gen_reg_rtx(SImode);

      // XXXX If this were done higher up (before LICM), there could be code
      // re-use/optimization

      int base = TT_OP_WH_SFPLOADI(0, SFPLOADI_MOD0_LOWER, 0);
      emit_insn(gen_ashlsi3(riscv_tmp, imm, GEN_INT(16)));
      emit_insn(gen_lshrsi3(riscv_tmp, riscv_tmp, GEN_INT(16)));
      riscv_sfpu_emit_nonimm_dst(addr, dst, 0, lv, riscv_tmp, base, 16, 16, 20);

      base = TT_OP_WH_SFPLOADI(0, SFPLOADI_MOD0_UPPER, 0);
      emit_insn(gen_lshrsi3(riscv_tmp, imm, GEN_INT(16)));
      riscv_sfpu_emit_nonimm_dst(addr, dst, 0, dst, riscv_tmp, base, 16, 16, 20);
    }
  } else {
    if (GET_CODE(imm) == CONST_INT) {
      emit_insn(gen_riscv_wh_sfploadi_int(dst, lv, GEN_INT(int_mod), riscv_sfpu_clamp_signed(imm, 0x7FFF)));
    } else {
      int base = TT_OP_WH_SFPLOADI(0, int_mod, 0);
      riscv_sfpu_emit_nonimm_dst(addr, dst, 0, lv, imm, base, 16, 16, 20);
    }
  }
}

void riscv_sfpu_wh_emit_sfpiadd_i(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod)
{
  if (GET_CODE(imm) == CONST_INT) {
    emit_insn(gen_riscv_wh_sfpiadd_i_int(dst, lv, src, riscv_sfpu_clamp_signed(imm, 0x7FF), mod));
  } else {
    int mod1 = INTVAL(mod);
    int base = TT_OP_WH_SFPIADD(0, 0, 0, mod1);
    riscv_sfpu_emit_nonimm_dst_src(addr, dst, 0, lv, src, imm, base, 20, 8, 4, 8);
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
void riscv_sfpu_wh_emit_sfpxiadd_i(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod)
{
  unsigned int modi = INTVAL(mod);
  unsigned int cmp = modi & SFPXCMP_MOD1_CC_MASK;
  unsigned int base_mod = modi & ~SFPXCMP_MOD1_CC_MASK;

  // Decompose aggregate comparisons, recurse
  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT) {
    riscv_sfpu_wh_emit_sfpxiadd_i(dst, lv, addr, src, imm, GEN_INT(base_mod | SFPXCMP_MOD1_CC_GTE));
    riscv_sfpu_wh_emit_sfpxiadd_i(dst, lv, addr, dst, GEN_INT(0), GEN_INT(base_mod | SFPXCMP_MOD1_CC_NE));
    if (cmp == SFPXCMP_MOD1_CC_LTE) {
      emit_insn(gen_riscv_wh_sfpcompc());
    }
    return;
  }

  bool need_loadi = true;
  bool is_signed = ((modi & SFPXIADD_MOD1_SIGNED) == SFPXIADD_MOD1_SIGNED);
  bool is_12bits = modi & SFPXIADD_MOD1_IS_12BITS;
  bool is_const_int = GET_CODE(imm) == CONST_INT;
  bool is_sub = ((modi & SFPXIADD_MOD1_IS_SUB) != 0);
  bool dst_unused = ((modi & SFPXIADD_MOD1_DST_UNUSED) != 0);
  int iv = is_const_int ? INTVAL(imm) : 0xffffffff;

  // Figure out if we need to do a loadi (>12 bits signed)
  if (is_const_int) {
    iv = is_sub ? -iv : iv;
    if (!(iv >= 2048 || iv < -2048)) {
      need_loadi = false;
      imm = GEN_INT(iv);
    }
  } else if (is_12bits) {
    need_loadi = false;
  }

  rtx set_cc_arg = src;

  bool need_setcc = ((cmp & SFPXCMP_MOD1_CC_MASK) != 0);
  if (need_loadi) {
    // Load imm into dst
    int loadi_mod = is_signed ? SFPXLOADI_MOD0_INT32 : SFPXLOADI_MOD0_UINT32;
    riscv_sfpu_wh_emit_sfpxloadi(dst, riscv_sfpu_gen_const0_vector(), addr, GEN_INT(loadi_mod), imm);

    unsigned int mod1 = is_sub ? SFPIADD_MOD1_ARG_2SCOMP_LREG_DST : SFPIADD_MOD1_ARG_LREG_DST;
    if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GTE) {
      // Perform op w/ compare
      mod1 |= (cmp == SFPXCMP_MOD1_CC_LT) ? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
      emit_insn(gen_riscv_wh_sfpiadd_v(dst, dst, src, GEN_INT(mod1)));
      need_setcc = false;
    } else {
      // Perform op w/o compare, compare with SETCC
      mod1 |= SFPIADD_MOD1_CC_NONE;
      emit_insn(gen_riscv_wh_sfpiadd_v(dst, dst, src, GEN_INT(mod1)));
      set_cc_arg = dst;
    }
  } else if (is_const_int) {
    if (iv != 0) {
      if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GTE) {
	// Perform op w/ compare
	unsigned int mod1 = (cmp == SFPXCMP_MOD1_CC_LT) ? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
	emit_insn(gen_riscv_wh_sfpiadd_i_int(dst, lv, src, imm, GEN_INT(mod1 | SFPIADD_MOD1_ARG_IMM)));
	need_setcc = false;
      } else {
	// Perform op w/o compare
	emit_insn(gen_riscv_wh_sfpiadd_i_int(dst, lv, src, imm,
					  GEN_INT(SFPIADD_MOD1_ARG_IMM | SFPIADD_MOD1_CC_NONE)));
	set_cc_arg = dst;
      }
    } else if (!dst_unused) {
      // An add or subtract against 0 isn't particularly interesting, but
      // we need to keep the register usage correct since dst is now src
      emit_insn(gen_riscv_wh_sfpiadd_i_int(dst, lv, src, imm,
                                        GEN_INT(SFPIADD_MOD1_ARG_IMM | SFPIADD_MOD1_CC_NONE)));
    }
  } else {
    // This code path handles the case where the operand isn't a CONST_INT (so
    // the value isn't known at compile time) but some (future) mechanism
    // (wrapper API or pragma) ensures that the resulting value fits in 12
    // bits and so an IADDI can be used.

    // The code below isn't used yet and doesn't handle negation properly
    gcc_assert(is_12bits);
    gcc_assert(false);
    unsigned int mod1 = SFPIADD_MOD1_ARG_IMM;
    if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GTE) {
      // Perform op w/ compare
      mod1 |= (cmp == SFPXCMP_MOD1_CC_LT) ? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
      need_setcc = false;
    } else {
      set_cc_arg = dst;
    }
    riscv_sfpu_wh_emit_sfpiadd_i(dst, lv, addr, src, imm, GEN_INT(mod1));
  }

  if (need_setcc) {
    emit_insn(gen_riscv_wh_sfpsetcc_v(set_cc_arg, GEN_INT(riscv_sfpu_cmp_ex_to_setcc_mod1_map[cmp])));
  }
}

// See comment block above sfpiadd_i_ex
void riscv_sfpu_wh_emit_sfpxiadd_v(rtx dst, rtx srcb, rtx srca, rtx mod)
{
  unsigned int modi = INTVAL(mod);
  unsigned int cmp = modi & SFPXCMP_MOD1_CC_MASK;
  unsigned int base_mod = modi & ~SFPXCMP_MOD1_CC_MASK;

  // Decompose aggregate comparisons, recurse
  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT) {
    riscv_sfpu_wh_emit_sfpxiadd_v(dst, srcb, srca, GEN_INT(base_mod | SFPXCMP_MOD1_CC_GTE));
    emit_insn(gen_riscv_wh_sfpsetcc_v(dst, GEN_INT(SFPSETCC_MOD1_LREG_NE0)));
    if (cmp == SFPXCMP_MOD1_CC_LTE) {
      emit_insn(gen_riscv_wh_sfpcompc());
    }
    return;
  }

  bool is_sub = ((modi & SFPXIADD_MOD1_IS_SUB) != 0);
  unsigned int mod1 = is_sub ? SFPIADD_MOD1_ARG_2SCOMP_LREG_DST : SFPIADD_MOD1_ARG_LREG_DST;

  if (cmp == SFPXCMP_MOD1_CC_LT || cmp == SFPXCMP_MOD1_CC_GTE) {
    // Perform op w/ compare
    mod1 |= (cmp == SFPXCMP_MOD1_CC_LT) ? SFPIADD_MOD1_CC_LT0 : SFPIADD_MOD1_CC_GTE0;
    emit_insn(gen_riscv_wh_sfpiadd_v(dst, srcb, srca, GEN_INT(mod1)));
  } else {
    // Perform op w/o compare
    mod1 |= SFPIADD_MOD1_CC_NONE;
    emit_insn(gen_riscv_wh_sfpiadd_v(dst, srcb, srca, GEN_INT(mod1)));
    if (cmp != 0) {
      // Must be EQ0 or NE0, compare with SETCC
      emit_insn(gen_riscv_wh_sfpsetcc_v(dst, GEN_INT(riscv_sfpu_cmp_ex_to_setcc_mod1_map[cmp])));
    }
  }
}

void riscv_sfpu_wh_emit_sfpxfcmps(rtx addr, rtx v, rtx f, rtx mod)
{
  bool need_sub = false;
  rtx ref_val = gen_reg_rtx(V64SFmode);
  int int_mod = INTVAL(mod);

  if (GET_CODE(f) == CONST_INT) {
    unsigned int fval = INTVAL(f);
    // Wrapper will convert 0 to -0
    unsigned int fmt = int_mod & SFPXSCMP_MOD1_FMT_MASK;
    if (fval != 0 &&
	((fmt != SFPXSCMP_MOD1_FMT_FLOAT && fval != 0x80000000) ||
	 (fmt == SFPXSCMP_MOD1_FMT_FLOAT && fval != 0x8000))) {
      need_sub = true;
      if ((fmt == SFPXSCMP_MOD1_FMT_FLOAT && fval == 0x3f800000) ||
	  (fmt != SFPXSCMP_MOD1_FMT_FLOAT && fval == 0x3f80)) {
	riscv_sfpu_emit_sfpassignlr(ref_val, GEN_INT(CREG_IDX_1));
      } else if ((fmt == SFPXSCMP_MOD1_FMT_FLOAT && fval == 0xbf800000) ||
		 (fmt != SFPXSCMP_MOD1_FMT_FLOAT && fval == 0xbf80)) {
	riscv_sfpu_emit_sfpassignlr(ref_val, GEN_INT(CREG_IDX_NEG_1));
      } else {
	riscv_sfpu_wh_emit_sfpxloadi(ref_val, riscv_sfpu_gen_const0_vector(), addr,
				     GEN_INT(riscv_sfpu_scmp2loadi_mod(fmt)), f);
      }
    }
  } else {
      need_sub = true;
      riscv_sfpu_wh_emit_sfpxloadi(ref_val, riscv_sfpu_gen_const0_vector(), addr,
				   GEN_INT(riscv_sfpu_scmp2loadi_mod(INTVAL(mod))), f);
  }

  unsigned int cmp = INTVAL(mod) & SFPXCMP_MOD1_CC_MASK;
  rtx setcc_mod = GEN_INT(riscv_sfpu_cmp_ex_to_setcc_mod1_map[cmp]);
  if (need_sub) {
    rtx neg_one = gen_reg_rtx(V64SFmode);
    rtx tmp = gen_reg_rtx(V64SFmode);
    riscv_sfpu_emit_sfpassignlr(neg_one, GEN_INT(CREG_IDX_NEG_1));
    emit_insn(gen_riscv_wh_sfpmad(tmp, ref_val, neg_one, v, GEN_INT(0)));
    emit_insn(gen_riscv_wh_sfpnop());
    v = tmp;
  }

  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT) {
    emit_insn(gen_riscv_wh_sfpsetcc_v(v, GEN_INT(SFPSETCC_MOD1_LREG_GTE0)));
    emit_insn(gen_riscv_wh_sfpsetcc_v(v, GEN_INT(SFPSETCC_MOD1_LREG_NE0)));
    if (cmp == SFPXCMP_MOD1_CC_LTE) {
      emit_insn(gen_riscv_wh_sfpcompc());
    }
  } else {
    emit_insn(gen_riscv_wh_sfpsetcc_v(v, setcc_mod));
  }
}

// Compare two vectors by subtracting v2 from v1 and doing a setcc
void riscv_sfpu_wh_emit_sfpxfcmpv(rtx v1, rtx v2, rtx mod)
{
  rtx tmp = gen_reg_rtx(V64SFmode);
  rtx neg1 = gen_reg_rtx(V64SFmode);

  riscv_sfpu_emit_sfpassignlr(neg1, GEN_INT(CREG_IDX_NEG_1));
  emit_insn(gen_riscv_wh_sfpmad(tmp, v2, neg1, v1, GEN_INT(0)));
  emit_insn(gen_riscv_wh_sfpnop());

  unsigned int cmp = INTVAL(mod) & SFPXCMP_MOD1_CC_MASK;
  if (cmp == SFPXCMP_MOD1_CC_LTE || cmp == SFPXCMP_MOD1_CC_GT) {
    emit_insn(gen_riscv_wh_sfpsetcc_v(tmp, GEN_INT(SFPSETCC_MOD1_LREG_GTE0)));
    emit_insn(gen_riscv_wh_sfpsetcc_v(tmp, GEN_INT(SFPSETCC_MOD1_LREG_NE0)));
    if (cmp == SFPXCMP_MOD1_CC_LTE) {
      emit_insn(gen_riscv_wh_sfpcompc());
    }
  } else {
    emit_insn(gen_riscv_wh_sfpsetcc_v(tmp, GEN_INT(riscv_sfpu_cmp_ex_to_setcc_mod1_map[INTVAL(mod)])));
  }
}

void riscv_sfpu_wh_emit_sfpdivp2(rtx dst, rtx lv, rtx addr, rtx imm, rtx src, rtx mod)
{
  if (GET_CODE(imm) == CONST_INT) {
    emit_insn(gen_riscv_wh_sfpdivp2_int(dst, lv, riscv_sfpu_clamp_signed(imm, 0x7FF), src, mod));
  } else {
    int base = TT_OP_WH_SFPDIVP2(0, 0, 0, INTVAL(mod));
    riscv_sfpu_emit_nonimm_dst_src(addr, dst, 0, lv, src, imm, base, 20, 8, 4, 8);
  }
}

void riscv_sfpu_wh_emit_sfpstochrnd_i(rtx dst, rtx lv, rtx addr, rtx mode, rtx imm, rtx src, rtx mod)
{
  if (GET_CODE(imm) == CONST_INT) {
    emit_insn(gen_riscv_wh_sfpstochrnd_i_int(dst, lv, mode, riscv_sfpu_clamp_unsigned(imm, 0x1F), src, mod));
  } else {
    int mod1 = INTVAL(mod);
    int imode = INTVAL(mode);
    int base = TT_OP_WH_SFP_STOCH_RND(imode, 0, 0, 0, 0, mod1);
    riscv_sfpu_emit_nonimm_dst_src(addr, dst, 0, lv, src, imm, base, 24, 8, 4, 8);
  }
}

void riscv_sfpu_wh_emit_sfpsetman(rtx dst, rtx lv, rtx addr, rtx imm, rtx src, rtx mod)
{
  if (GET_CODE(imm) == CONST_INT) {
    unsigned int iv = INTVAL(imm);
    if (iv > 4095) {
      riscv_sfpu_wh_emit_sfpxloadi(dst, lv, addr,
				   GEN_INT(SFPXLOADI_MOD0_UINT32), imm);
      emit_insn(gen_riscv_wh_sfpsetman_v(dst, dst, src));
    } else {
      emit_insn (gen_riscv_wh_sfpsetman_i_int(dst, lv, imm, src));
    }
  } else {
    if (INTVAL(mod) == SFPXSETMAN_MOD1_16BITIMM) {
      int base = TT_OP_WH_SFPSETMAN(0, 0, 0, 1);
      riscv_sfpu_emit_nonimm_dst_src(addr, dst, 0, lv, src, imm, base, 20, 8, 4, 8);
    } else {
      riscv_sfpu_wh_emit_sfpxloadi(dst, lv, addr,
				   GEN_INT(SFPXLOADI_MOD0_UINT32), imm);
      emit_insn(gen_riscv_wh_sfpsetman_v(dst, dst, src));
    }
  }
}

void riscv_sfpu_wh_emit_sfpshft2_e(rtx dst, rtx live, rtx src, rtx mod)
{
  int modi = INTVAL(mod);

  // This routine handles a subset of mod values that all require a NOP
  gcc_assert(modi == 3 || modi == 4);

  if (modi == 4) {
    // WH_B0 HW bug (issue #3240): the shftr version of the ins doesn't set the
    // value shifted into place to 0 but instead uses the previous value (eg,
    // from a ror) Here we clear that value to 0 by rotating in the 0 register
    // Optimization potential to not do this if the previous insn was a shftr

    rtx live_const = riscv_sfpu_gen_const0_vector();
    rtx lreg9 = gen_reg_rtx(V64SFmode);
    SET_REGNO(lreg9, SFPU_REG_FIRST + 9);
    emit_insn (gen_riscv_wh_sfpshft2_e_int(lreg9, live_const, lreg9, GEN_INT(3)));
    emit_insn(gen_riscv_wh_sfpnop());
  }

  emit_insn (gen_riscv_wh_sfpshft2_e_int(dst, live, src, mod));
  emit_insn(gen_riscv_wh_sfpnop());
}
