#define INCLUDE_STRING
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "regs.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "recog.h"
#include "output.h"
#include "alias.h"
#include "tree.h"
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
#include "sfpu-protos.h"

rtx riscv_sfpu_gen_const0_vector()
{
    int i;
    rtx vec[64];

    for (i = 0; i < 64; i++) {
      vec[i] = const_double_from_real_value(dconst0, SFmode);
    }

    return gen_rtx_CONST_VECTOR(V64SFmode, gen_rtvec_v(64, vec));
}

void riscv_sfpu_emit_nonimm_dst(rtx buf_addr, rtx dst, int nnops, rtx imm, int base, int lshft, int rshft, int dst_shft)
{
    rtx insn = gen_reg_rtx(SImode);
    emit_insn(gen_ashlsi3(insn, imm, GEN_INT(lshft)));
    emit_insn(gen_lshrsi3(insn, insn, GEN_INT(rshft)));

    rtx live = riscv_sfpu_gen_const0_vector();
    emit_insn(gen_riscv_sfpnonimm_dst(dst, buf_addr, GEN_INT(nnops), live, GEN_INT(base), GEN_INT(dst_shft), insn));
}

void riscv_sfpu_emit_nonimm_dst_lv(rtx buf_addr, rtx dst, int nnops, rtx dst_lv, rtx imm, int base, int lshft, int rshft, int dst_shft)
{
    rtx insn = gen_reg_rtx(SImode);
    emit_insn(gen_ashlsi3(insn, imm, GEN_INT(lshft)));
    emit_insn(gen_lshrsi3(insn, insn, GEN_INT(rshft)));
    emit_insn(gen_riscv_sfpnonimm_dst_lv(dst, buf_addr, GEN_INT(nnops), dst_lv, GEN_INT(base), GEN_INT(dst_shft), insn));
}

void riscv_sfpu_emit_nonimm_dst_src(rtx buf_addr, rtx dst, int nnops, rtx src, rtx imm, int base, int lshft, int rshft, int dst_shft, int src_shft)
{
    rtx insn = gen_reg_rtx(SImode);
    emit_insn(gen_ashlsi3(insn, imm, GEN_INT(lshft)));
    emit_insn(gen_lshrsi3(insn, insn, GEN_INT(rshft)));

    rtx live = riscv_sfpu_gen_const0_vector();
    emit_insn(gen_riscv_sfpnonimm_dst_src(dst, buf_addr, GEN_INT(nnops), live, src, GEN_INT(base), GEN_INT(dst_shft), GEN_INT(src_shft), insn));
}

void riscv_sfpu_emit_nonimm_dst_src_lv(rtx buf_addr, rtx dst, int nnops, rtx src, rtx dst_lv, rtx imm, int base, int lshft, int rshft, int dst_shft, int src_shft)
{
    rtx insn = gen_reg_rtx(SImode);
    emit_insn(gen_ashlsi3(insn, imm, GEN_INT(lshft)));
    emit_insn(gen_lshrsi3(insn, insn, GEN_INT(rshft)));
    emit_insn(gen_riscv_sfpnonimm_dst_src(dst, buf_addr, GEN_INT(nnops), dst_lv, src, GEN_INT(base), GEN_INT(dst_shft), GEN_INT(src_shft), insn));
}

void riscv_sfpu_emit_nonimm_src(rtx buf_addr, rtx src, int nnops, rtx imm, int base, int lshft, int rshft, int src_shft)
{
    rtx insn = gen_reg_rtx(SImode);
    emit_insn(gen_ashlsi3(insn, imm, GEN_INT(lshft)));
    emit_insn(gen_lshrsi3(insn, insn, GEN_INT(rshft)));
    emit_insn(gen_riscv_sfpnonimm_src(src, buf_addr, GEN_INT(nnops), GEN_INT(base), GEN_INT(src_shft), insn));
}

void riscv_sfpu_emit_nonimm_store(rtx buf_addr, rtx src, int nnops, rtx imm, int base, int lshft, int rshft, int src_shft)
{
    // This is exactly like _src, exists so peephole can handle the store-to-load nop
    rtx insn = gen_reg_rtx(SImode);
    emit_insn(gen_ashlsi3(insn, imm, GEN_INT(lshft)));
    emit_insn(gen_lshrsi3(insn, insn, GEN_INT(rshft)));
    emit_insn(gen_riscv_sfpnonimm_store(src, buf_addr, GEN_INT(nnops), GEN_INT(base), GEN_INT(src_shft), insn));
}

char * riscv_sfpu_output_nonimm_store_and_nops(char *sw, int nnops, rtx operands[])
{
  char *out = sw;
  while (nnops-- > 0) {
     output_asm_insn(out, operands);
     out = "SFPNOP";
  }
  return out;
}
