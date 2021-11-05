#ifndef GCC_RISCV_SFPU_PROTOS_H
#define GCC_RISCV_SFPU_PROTOS_H

#include "sfpu_ops.h"

extern rtx riscv_sfpu_gen_const0_vector();
extern void riscv_sfpu_emit_nonimm_dst(rtx buf_addr, rtx dst, int nnops, rtx imm, int base, int lshft, int rshft, int dst_shft);
extern void riscv_sfpu_emit_nonimm_dst_lv(rtx buf_addr, rtx dst, int nnops, rtx dst_lv, rtx imm, int base, int lshft, int rshft, int dst_shft);
extern void riscv_sfpu_emit_nonimm_dst_src(rtx buf_addr, rtx dst, int nnops, rtx src, rtx imm, int base, int lshft, int rshft, int dst_shft, int src_shft);
void riscv_sfpu_emit_nonimm_dst_src_lv(rtx buf_addr, rtx dst, int nnops, rtx src, rtx dst_lv, rtx imm, int base, int lshft, int rshft, int dst_shft, int src_shft);
extern void riscv_sfpu_emit_nonimm_src(rtx buf_addr, rtx src, int nnops, rtx imm, int base, int lshft, int rshft, int src_shft);
extern void riscv_sfpu_emit_nonimm_store(rtx buf_addr, rtx src, int nnops, rtx imm, int base, int lshft, int rshft, int src_shft);
extern char * riscv_sfpu_output_nonimm_store_and_nops(char *sw, int nnops, rtx operands[]);

#endif /* ! GCC_RISCV_SFPU_PROTOS_H */
