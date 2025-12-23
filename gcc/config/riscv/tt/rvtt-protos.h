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

constexpr unsigned int INSN_SCHED_NOP_MASK = 0x0F;  // overloaded low bits contain # nops
constexpr unsigned int INSN_SCHED_DYN      = 0x20;  // dynamic scheduling flag
constexpr unsigned int INSN_SCHED_DYN_DEP  = 0x40;  // has a dependency on a prior insn type

extern rtx rvtt_clamp_signed(rtx v, unsigned int mask);
extern rtx rvtt_clamp_unsigned(rtx v, unsigned int mask);

extern void rvtt_mov_error (const rtx_insn *, bool is_load) ATTRIBUTE_NORETURN ATTRIBUTE_COLD;

// We use this value to indicate 'not a register'
extern rtx rvtt_vec0_rtx;

// Instruction synthesis
char const *rvtt_synth_insn_pattern (rtx operands[], unsigned);
rtx rvtt_sfpsynth_insn_dst (rtx addr, int icode, unsigned flags, rtx synth, unsigned opcode, rtx id,
			    rtx src, unsigned src_shift, rtx dst, unsigned dst_shift, rtx lv);
inline rtx rvtt_sfpsynth_insn_dst (rtx addr, int icode, unsigned flags, rtx synth, unsigned opcode, rtx id,
				   rtx dst, unsigned dst_shift, rtx lv)
{
  return rvtt_sfpsynth_insn_dst (addr, icode, flags, synth, opcode, id,
				 rvtt_vec0_rtx, 0, dst, dst_shift, lv);
}
rtx rvtt_sfpsynth_insn (rtx addr, int icode, unsigned flags, rtx synth, unsigned opcode, rtx id,
			rtx src, unsigned src_shift);
inline rtx rvtt_sfpsynth_insn (rtx addr, int icode, unsigned flags, rtx synth, unsigned opcode, rtx id)
{
  return rvtt_sfpsynth_insn (addr, icode, flags, synth, opcode, id, rvtt_vec0_rtx, 0);
}
rtx rvtt_sfpsynth_store_insn (rtx addr, int icode, unsigned flags, rtx synth, unsigned opcode, rtx id,
			      rtx src, unsigned src_shift);

extern rtx rvtt_gen_rtx_creg (machine_mode, unsigned sfpu_regno);

extern void rvtt_wh_emit_sfpload(rtx dst, rtx lv, rtx addr, rtx mod, rtx mode, rtx imm, rtx nonimm, rtx id);
extern void rvtt_wh_emit_sfpxloadi(rtx dst, rtx lv, rtx addr, rtx mod, rtx imm, rtx nonimm, rtx id);
extern void rvtt_wh_emit_sfpiadd_i(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod, rtx nonimm, rtx id);
extern void rvtt_wh_emit_sfpxiadd_i(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod, bool dst_used = false);
extern void rvtt_wh_emit_sfpxiadd_v(rtx dst, rtx srcb, rtx srca, rtx mod);
extern void rvtt_wh_emit_sfpxfcmps(rtx addr, rtx v1, rtx f, rtx mod);
extern void rvtt_wh_emit_sfpxfcmpv(rtx v1, rtx v2, rtx mod);
extern void rvtt_wh_emit_sfpdivp2(rtx dst, rtx lv, rtx addr, rtx imm, rtx src, rtx mod, rtx nonimm, rtx id);
extern void rvtt_wh_emit_sfpstochrnd_i(rtx dst, rtx lv, rtx addr, rtx mode, rtx imm, rtx src, rtx mod, rtx nonimm, rtx id);
extern void rvtt_wh_emit_sfpsetman(rtx dst, rtx lv, rtx addr, rtx imm, rtx src);
extern void rvtt_wh_emit_sfpshft2_e(rtx dst, rtx lv, rtx src, rtx mod);

extern void rvtt_bh_emit_sfpload(rtx dst, rtx lv, rtx addr, rtx mod, rtx mode, rtx imm, rtx nonimm, rtx id);
extern void rvtt_bh_emit_sfpxloadi(rtx dst, rtx lv, rtx addr, rtx mod, rtx imm, rtx nonimm, rtx id);
extern void rvtt_bh_emit_sfpiadd_i(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod, rtx nonimm, rtx id);
extern void rvtt_bh_emit_sfpxiadd_i(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod, bool dst_used = false);
extern void rvtt_bh_emit_sfpxiadd_v(rtx dst, rtx srcb, rtx srca, rtx mod);
extern void rvtt_bh_emit_sfpxfcmps(rtx addr, rtx v1, rtx f, rtx mod);
extern void rvtt_bh_emit_sfpxfcmpv(rtx v1, rtx v2, rtx mod);
extern void rvtt_bh_emit_sfpdivp2(rtx dst, rtx lv, rtx addr, rtx imm, rtx src, rtx mod, rtx nonimm, rtx id);
extern void rvtt_bh_emit_sfpstochrnd_i(rtx dst, rtx lv, rtx addr, rtx mode, rtx imm, rtx src, rtx mod, rtx nonimm, rtx id);
extern void rvtt_bh_emit_sfpsetman(rtx dst, rtx lv, rtx addr, rtx imm, rtx src);
extern void rvtt_bh_emit_sfpshft2_e(rtx dst, rtx lv, rtx src, rtx mod);

extern const char * rvtt_emit_testcode(rtx operands[]);
extern bool rvtt_hll_p(const rtx pat);
extern bool rvtt_l1_load_p(const rtx pat);
extern bool rvtt_reg_load_p(const rtx pat);

#endif /* ! GCC_RVTT_PROTOS_H */
