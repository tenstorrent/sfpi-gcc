/* TT .md file fn prototypes, etc
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
#ifndef GCC_RISCV_SFPU_PROTOS_H
#define GCC_RISCV_SFPU_PROTOS_H

#include "sfpu-ops-wh.h"
#include "sfpu-ops-bh.h"

#define rvtt_sfpu_regno(operand) (REGNO (operand) - SFPU_REG_FIRST)

extern rtx rvtt_clamp_signed(rtx v, unsigned int mask);
extern rtx rvtt_clamp_unsigned(rtx v, unsigned int mask);

extern void rvtt_mov_error (const rtx_insn *, bool is_load) ATTRIBUTE_NORETURN ATTRIBUTE_COLD;

extern rtx rvtt_gen_rtx_creg (machine_mode, unsigned sfpu_regno);
extern rtx rvtt_gen_rtx_noval (machine_mode);

// Instruction synthesis
char const *rvtt_synth_insn_pattern (rtx operands[], unsigned);
rtx rvtt_sfpsynth_insn_dst (rtx addr, int icode, unsigned flags, rtx synth, unsigned opcode, rtx id,
			    rtx src, unsigned src_shift, rtx dst, unsigned dst_shift, rtx lv);
inline rtx rvtt_sfpsynth_insn_dst (rtx addr, int icode, unsigned flags, rtx synth, unsigned opcode, rtx id,
				   rtx dst, unsigned dst_shift, rtx lv)
{
  return rvtt_sfpsynth_insn_dst (addr, icode, flags, synth, opcode, id,
				 rvtt_gen_rtx_noval (XTT32SImode), 0, dst, dst_shift, lv);
}
rtx rvtt_sfpsynth_insn (rtx addr, int icode, unsigned flags, rtx synth, unsigned opcode, rtx id,
			rtx src, unsigned src_shift);
inline rtx rvtt_sfpsynth_insn (rtx addr, int icode, unsigned flags, rtx synth, unsigned opcode, rtx id)
{
  return rvtt_sfpsynth_insn (addr, icode, flags, synth, opcode, id, rvtt_gen_rtx_noval (XTT32SImode), 0);
}
rtx rvtt_sfpsynth_store_insn (rtx addr, int icode, unsigned flags, rtx synth, unsigned opcode, rtx id,
			      rtx src, unsigned src_shift);

extern void rvtt_emit_sfpxloadi_wh(rtx dst, rtx lv, rtx addr, rtx mod, rtx imm, rtx nonimm, rtx id);
extern void rvtt_emit_sfpiadd_i_wh(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod, rtx nonimm, rtx id);
extern void rvtt_emit_sfpxiadd_i_wh(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod, bool dst_used = false);
extern void rvtt_emit_sfpxiadd_v_wh(rtx dst, rtx srcb, rtx srca, rtx mod);
extern void rvtt_emit_sfpxfcmps_wh(rtx addr, rtx v1, rtx f, rtx mod);
extern void rvtt_emit_sfpxfcmpv_wh(rtx v1, rtx v2, rtx mod);

extern void rvtt_emit_sfpxloadi_bh(rtx dst, rtx lv, rtx addr, rtx mod, rtx imm, rtx nonimm, rtx id);
extern void rvtt_emit_sfpiadd_i_bh(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod, rtx nonimm, rtx id);
extern void rvtt_emit_sfpxiadd_i_bh(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod, bool dst_used = false);
extern void rvtt_emit_sfpxiadd_v_bh(rtx dst, rtx srcb, rtx srca, rtx mod);
extern void rvtt_emit_sfpxfcmps_bh(rtx addr, rtx v1, rtx f, rtx mod);
extern void rvtt_emit_sfpxfcmpv_bh(rtx v1, rtx v2, rtx mod);

extern const char * rvtt_emit_testcode(rtx operands[]);
extern bool rvtt_hll_p(const rtx pat);
extern bool rvtt_l1_load_p(const rtx pat);
extern bool rvtt_reg_load_p(const rtx pat);

// Gimple passes
class gimple_opt_pass;
extern gimple_opt_pass *make_pass_rvtt_attrib (gcc::context *ctxt);
extern gimple_opt_pass *make_pass_rvtt_cc (gcc::context *ctxt);
extern gimple_opt_pass *make_pass_rvtt_combine (gcc::context *ctxt);
extern gimple_opt_pass *make_pass_rvtt_unspec_prop_ssa (gcc::context *ctxt);
extern gimple_opt_pass *make_pass_rvtt_expand (gcc::context *ctxt);
extern gimple_opt_pass *make_pass_rvtt_live (gcc::context *ctxt);
extern gimple_opt_pass *make_pass_rvtt_move (gcc::context *ctxt);
extern gimple_opt_pass *make_pass_rvtt_synth_expand (gcc::context *ctxt);
extern gimple_opt_pass *make_pass_rvtt_synth_renumber (gcc::context *ctxt);
extern gimple_opt_pass *make_pass_rvtt_synth_split (gcc::context *ctxt);
extern gimple_opt_pass *make_pass_rvtt_synth_nullify (gcc::context *ctxt);
extern gimple_opt_pass *make_pass_rvtt_warn (gcc::context *ctxt);

// RTL passes
class rtl_opt_pass;
extern rtl_opt_pass *make_pass_rvtt_fix_wh (gcc::context *ctxt);
extern rtl_opt_pass *make_pass_rvtt_hll (gcc::context *ctxt);
extern rtl_opt_pass *make_pass_rvtt_replay (gcc::context *ctxt);
extern rtl_opt_pass *make_pass_rvtt_rmext (gcc::context *ctxt);
extern rtl_opt_pass *make_pass_rvtt_schedule (gcc::context *ctxt);
extern rtl_opt_pass *make_pass_rvtt_synth_opcode (gcc::context *ctxt);
extern rtl_opt_pass *make_pass_rvtt_unspec_prop_rtl (gcc::context *ctxt);

constexpr unsigned int SFPMAD_MOD1_OFFSET_NONE = 0;
// A * B + C
constexpr unsigned int SFPMAD_MOD1_BH_COMPL_A = 1; // negate A operand
constexpr unsigned int SFPMAD_MOD1_BH_COMPL_C = 2; // negate C operand

constexpr unsigned int SFPMOV_MOD1_NONE = 0;
constexpr unsigned int SFPMOV_MOD1_COMPL = 1; // negate
constexpr unsigned int SFPMOV_MOD1_ALL = 2; // copy all lanes
constexpr unsigned int SFPMOV_MOD1_CFG = 8; // read cfg register

constexpr unsigned int SFPLOADI_IMM_ARG_POS = 2;
constexpr unsigned int SFPLOADI_LV_IMM_ARG_POS = 3;
constexpr unsigned int SFPLOADI_MOD0_FLOATB = 0;
constexpr unsigned int SFPLOADI_MOD0_FLOATA = 1;
constexpr unsigned int SFPLOADI_MOD0_USHORT = 2;
constexpr unsigned int SFPLOADI_MOD0_SHORT = 4;
constexpr unsigned int SFPLOADI_MOD0_UPPER = 8;
constexpr unsigned int SFPLOADI_MOD0_LOWER = 10;
constexpr unsigned int SFPXLOADI_MOD0_32BIT_MASK = 16;
constexpr unsigned int SFPXLOADI_MOD0_INT32 = 16;
constexpr unsigned int SFPXLOADI_MOD0_UINT32 = 17;
constexpr unsigned int SFPXLOADI_MOD0_FLOAT = 18;
constexpr unsigned int SFPXLOADI_IMM_POS = 2;

constexpr unsigned int SFPEXEXP_MOD1_DEBIAS = 0;
constexpr unsigned int SFPEXEXP_MOD1_NODEBIAS = 1;
constexpr unsigned int SFPEXEXP_MOD1_SET_CC_SGN_EXP = 2;
constexpr unsigned int SFPEXEXP_MOD1_SET_CC_COMP_EXP = 8;
constexpr unsigned int SFPEXEXP_MOD1_SET_CC_SGN_COMP_EXP = 10;

constexpr unsigned int SFPSETCC_MOD1_LREG_LT0 = 0;
constexpr unsigned int SFPSETCC_MOD1_IMM_BIT0 = 1;
constexpr unsigned int SFPSETCC_MOD1_LREG_NE0 = 2;
constexpr unsigned int SFPSETCC_MOD1_LREG_GTE0 = 4;
constexpr unsigned int SFPSETCC_MOD1_LREG_EQ0 = 6;
constexpr unsigned int SFPSETCC_MOD1_COMP = 8;

constexpr unsigned int SFPPUSHCC_MOD1_PUSH = 0;
constexpr unsigned int SFPPUSHCC_MOD1_REPLACE = 1;

constexpr unsigned int SFPPOPCC_MOD1_POP = 0;

constexpr unsigned int SFPLZ_MOD1_CC_NONE = 0;
constexpr unsigned int SFPLZ_MOD1_CC_NE0 = 2;
constexpr unsigned int SFPLZ_MOD1_CC_COMP = 8;
constexpr unsigned int SFPLZ_MOD1_CC_EQ0 = 10;
constexpr unsigned int SFPLZ_MOD1_NOSGN_MASK = 4;
constexpr unsigned int SFPLZ_MOD1_NOSGN_CC_NONE = 4;
constexpr unsigned int SFPLZ_MOD1_NOSGN_CC_NE0 = 6;
constexpr unsigned int SFPLZ_MOD1_NOSGN_CC_COMP = 12;
constexpr unsigned int SFPLZ_MOD1_NOSGN_CC_EQ0 = 14;

constexpr unsigned int SFPCAST_MOD1_INT32_TO_FP32_RNE = 0;
constexpr unsigned int SFPCAST_MOD1_INT32_TO_FP32_RNS = 1;
// Added in BlackHole:
constexpr unsigned int SFPCAST_MOD1_SM32_TO_INT32 = 2;
constexpr unsigned int SFPCAST_MOD1_INT32_TO_SM32 = 3;

constexpr unsigned int SFPIADD_MOD1_ARG_LREG_DST = 0;
constexpr unsigned int SFPIADD_MOD1_ARG_IMM = 1;
constexpr unsigned int SFPIADD_MOD1_ARG_2SCOMP_LREG_DST = 2;
constexpr unsigned int SFPIADD_MOD1_CC_LT0 = 0;
constexpr unsigned int SFPIADD_MOD1_CC_NONE = 4;
constexpr unsigned int SFPIADD_MOD1_CC_GTE0 = 8;

constexpr unsigned int SFPXIADD_MOD1_SIGNED = 8;
constexpr unsigned int SFPXIADD_MOD1_IS_SUB = 16;
constexpr unsigned int SFPXIADD_MOD1_12BIT = 32;
constexpr unsigned int SFPXIADD_MOD1_16BIT = 64;
constexpr unsigned int SFPXIADD_MOD1_DST_UNUSED = 128;
constexpr unsigned int SFPXIADD_SRC_ARG_POS = 1;
constexpr unsigned int SFPXIADD_IMM_ARG_POS = 2;

constexpr unsigned int SFPXCMP_MOD1_CC_NONE = 0;
constexpr unsigned int SFPXCMP_MOD1_CC_LT = 1;
constexpr unsigned int SFPXCMP_MOD1_CC_EQ = 2;
constexpr unsigned int SFPXCMP_MOD1_CC_GTE = 3;
constexpr unsigned int SFPXCMP_MOD1_CC_NE = 4;
constexpr unsigned int SFPXCMP_MOD1_CC_LTE = 5;
constexpr unsigned int SFPXCMP_MOD1_CC_GT = 6;
constexpr unsigned int SFPXCMP_MOD1_CC_MASK = 7;

constexpr unsigned int SFPXSCMP_MOD1_FMT_A = 8;
constexpr unsigned int SFPXSCMP_MOD1_FMT_B = 16;
constexpr unsigned int SFPXSCMP_MOD1_FMT_FLOAT = 32;
constexpr unsigned int SFPXSCMP_MOD1_FMT_MASK = 0x38;
constexpr unsigned int SFPXSCMP_SRC_ARG_POS = 1;

constexpr unsigned int SFPXBOOL_MOD1_OR = 1;
constexpr unsigned int SFPXBOOL_MOD1_AND = 2;
constexpr unsigned int SFPXBOOL_MOD1_NOT = 3;
constexpr unsigned int SFPXBOOL_LEFT_TREE_ARG_POS = 1;
constexpr unsigned int SFPXBOOL_RIGHT_TREE_ARG_POS = 2;

constexpr unsigned int SFPXCONDB_TREE_ARG_POS = 0;
constexpr unsigned int SFPXCONDB_START_ARG_POS = 1;

constexpr unsigned int SFPXCONDI_TREE_ARG_POS = 0;

constexpr unsigned int SFPSHFT_MOD1_SHFT_IMM = 1;
constexpr unsigned int SFPSHFT_MOD1_SHFT_REG = 0;
// Added in BlackHole
constexpr unsigned int SFPSHFT_MOD1_LOGICAL = 0;
constexpr unsigned int SFPSHFT_MOD1_ARITHMETIC = 2;
constexpr unsigned int SFPSHFT_MOD1_SRC_LREG_C = 4;

constexpr unsigned int SFPSHFT2_MOD1_COPY4 = 0;
constexpr unsigned int SFPSHFT2_MOD1_SUBVEC_CHAINED_COPY4 = 1;
constexpr unsigned int SFPSHFT2_MOD1_SUBVEC_SHFLROR1_AND_COPY4 = 2;
constexpr unsigned int SFPSHFT2_MOD1_SUBVEC_SHFLROR1 = 3;
constexpr unsigned int SFPSHFT2_MOD1_SUBVEC_SHFLSHR1 = 4;
constexpr unsigned int SFPSHFT2_MOD1_SHFT_LREG = 5;
constexpr unsigned int SFPSHFT2_MOD1_SHFT_IMM = 6;

constexpr unsigned int CREG_IDX_0P837300003 = 8;
constexpr unsigned int CREG_IDX_0 = 9;
constexpr unsigned int CREG_IDX_1 = 10;
constexpr unsigned int CREG_IDX_NEG_1 = 11;
constexpr unsigned int CREG_IDX_0P001953125 = 12;
constexpr unsigned int CREG_IDX_NEG_0P67480469 = 13;
constexpr unsigned int CREG_IDX_NEG_0P34472656 = 14;
constexpr unsigned int CREG_IDX_TILEID = 15;

#endif /* ! GCC_RVTT_PROTOS_H */
