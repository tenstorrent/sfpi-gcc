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

void riscv_sfpu_emit_nonimm_dst(rtx buf_addr, rtx dst, int nnops, rtx dst_lv, rtx imm,
				int base, int lshft, int rshft, int dst_shft)
{
    rtx insn = gen_reg_rtx(SImode);
    emit_insn(gen_ashlsi3(insn, imm, GEN_INT(lshft)));
    emit_insn(gen_lshrsi3(insn, insn, GEN_INT(rshft)));
    emit_insn(gen_riscv_sfpnonimm_dst(dst, buf_addr, GEN_INT(nnops), dst_lv, GEN_INT(base), GEN_INT(dst_shft), insn));
}

void riscv_sfpu_emit_nonimm_dst_src(rtx buf_addr, rtx dst, int nnops, rtx dst_lv, rtx src, rtx imm, int base, int lshft, int rshft, int dst_shft, int src_shft)
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

void riscv_sfpu_emit_sfpload(rtx dst, rtx lv, rtx addr, rtx mod, rtx imm)
{
  if (GET_CODE(imm) == CONST_INT) {
    emit_insn(gen_riscv_sfpload_int(dst, lv, mod, imm));
  } else {
    int base = TT_OP_SFPLOAD(0, INTVAL(mod), 0);
    riscv_sfpu_emit_nonimm_dst(addr, dst, 0, lv, imm, base, 16, 16, 20);
  }
}

void riscv_sfpu_emit_sfploadi(rtx dst, rtx lv, rtx addr, rtx mod, rtx imm)
{
  if (GET_CODE(imm) == CONST_INT) {
    emit_insn(gen_riscv_sfploadi_int(dst, lv, mod, imm));
  } else {
    int base = TT_OP_SFPLOADI(0, INTVAL(mod), 0);
    riscv_sfpu_emit_nonimm_dst(addr, dst, 0, lv, imm, base, 16, 16, 20);
  }
}

void riscv_sfpu_emit_sfpiadd_i(rtx dst, rtx lv, rtx addr, rtx src, rtx imm, rtx mod)
{
  if (GET_CODE(imm) == CONST_INT) {
    emit_insn(gen_riscv_sfpiadd_i_int(dst, lv, src, imm, mod));
  } else {
    int mod1 = INTVAL(mod);
    int base = TT_OP_SFPIADD(0, 0, 0, mod1);
    int nnops = (mod1 < 3 || mod1 > 7) ? 3 : 2;
    riscv_sfpu_emit_nonimm_dst_src(addr, dst, nnops, lv, src, imm, base, 20, 8, 4, 8);
  }
}

void riscv_sfpu_emit_sfpdivp2(rtx dst, rtx lv, rtx addr, rtx imm, rtx src, rtx mod)
{
  if (GET_CODE(imm) == CONST_INT) {
    emit_insn(gen_riscv_sfpdivp2_int(dst, lv, imm, src, mod));
  } else {
    int base = TT_OP_SFPDIVP2(0, 0, 0, INTVAL(mod));
    riscv_sfpu_emit_nonimm_dst_src(addr, dst, 2, lv, src, imm, base, 20, 8, 4, 8);
  }
}
