#ifndef GCC_RISCV_SFPU_PROTOS_H
#define GCC_RISCV_SFPU_PROTOS_H

#include "sfpu-ops-gs.h"
#include "sfpu-ops-wh.h"

#define riscv_sfpu_regno(operand) (REGNO(operand) - SFPU_REG_FIRST)

extern const char* riscv_sfpu_lv_regno_str(char *str, rtx operand);

extern rtx riscv_sfpu_clamp_signed(rtx v, unsigned int mask);
extern rtx riscv_sfpu_clamp_unsigned(rtx v, unsigned int mask);
extern rtx riscv_sfpu_gen_const0_vector();

extern void riscv_sfpu_emit_nonimm_dst(rtx buf_addr, rtx dst, int nnops, rtx dst_lv,
				       rtx imm, int base, int lshft, int rshft, int dst_shft);
extern void riscv_sfpu_emit_nonimm_dst_src(rtx buf_addr, rtx dst, int nnops, rtx dst_lv, rtx src, rtx imm, int base, int lshft, int rshft, int dst_shft, int src_shft);
extern void riscv_sfpu_emit_nonimm_src(rtx buf_addr, rtx src, int nnops, rtx imm, int base, int lshft, int rshft, int src_shft);
extern void riscv_sfpu_emit_nonimm_store(rtx buf_addr, rtx src, int nnops, rtx imm, int base, int lshft, int rshft, int src_shft);
extern char const * riscv_sfpu_output_nonimm_store_and_nops(const char *sw, int nnops, rtx operands[]);

extern void riscv_sfpu_emit_sfpassignlr(rtx dst, rtx lr);

extern void riscv_sfpu_gs_emit_sfpload(rtx dst, rtx lv, rtx addr, rtx mod, rtx imm);
extern void riscv_sfpu_gs_emit_sfpxloadi(rtx dst, rtx lv, rtx addr, rtx mod, rtx imm);
extern void riscv_sfpu_gs_emit_sfpiadd_i(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod);
extern void riscv_sfpu_gs_emit_sfpxiadd_i(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod);
extern void riscv_sfpu_gs_emit_sfpxiadd_v(rtx dst, rtx srcb, rtx srca, rtx mod);
extern void riscv_sfpu_gs_emit_sfpxfcmps(rtx addr, rtx v1, rtx f, rtx mod);
extern void riscv_sfpu_gs_emit_sfpxfcmpv(rtx v1, rtx v2, rtx mod);
extern void riscv_sfpu_gs_emit_sfpdivp2(rtx dst, rtx lv, rtx addr, rtx imm, rtx src, rtx mod);

extern void riscv_sfpu_wh_emit_sfpload(rtx dst, rtx lv, rtx addr, rtx mod, rtx mode, rtx imm);
extern void riscv_sfpu_wh_emit_sfpxloadi(rtx dst, rtx lv, rtx addr, rtx mod, rtx imm);
extern void riscv_sfpu_wh_emit_sfpiadd_i(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod);
extern void riscv_sfpu_wh_emit_sfpxiadd_i(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod);
extern void riscv_sfpu_wh_emit_sfpxiadd_v(rtx dst, rtx srcb, rtx srca, rtx mod);
extern void riscv_sfpu_wh_emit_sfpxfcmps(rtx addr, rtx v1, rtx f, rtx mod);
extern void riscv_sfpu_wh_emit_sfpxfcmpv(rtx v1, rtx v2, rtx mod);
extern void riscv_sfpu_wh_emit_sfpdivp2(rtx dst, rtx lv, rtx addr, rtx imm, rtx src, rtx mod);
extern void riscv_sfpu_wh_emit_sfpstochrnd_i(rtx dst, rtx lv, rtx addr, rtx mode, rtx imm, rtx src, rtx mod);
extern void riscv_sfpu_wh_emit_sfpsetman(rtx dst, rtx lv, rtx addr, rtx imm, rtx src, rtx mod);
extern void riscv_sfpu_wh_emit_sfpshft2_e(rtx dst, rtx lv, rtx src, rtx mod);

#endif /* ! GCC_RISCV_SFPU_PROTOS_H */
