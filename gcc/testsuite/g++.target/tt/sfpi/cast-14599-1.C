// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
  extern unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void i2f_rne () {
  vInt a = l_reg[LRegs::LReg0];
  vFloat b = int32_to_float (a, false);
  l_reg[LRegs::LReg1] = b;
}
// { dg-final { scan-assembler {\n_Z7i2f_rnev:\n\tSFPCAST	L1, L0, 0\n\tret\n} } }

void i2f_rns () {
  vInt a = l_reg[LRegs::LReg0];
  vFloat b = int32_to_float (a, true);
  l_reg[LRegs::LReg1] = b;
}
// { dg-final { scan-assembler {\n_Z7i2f_rnsv:\n\tSFPCAST	L1, L0, 1\n\tret\n} } }

void sm2i () {
  vInt a = l_reg[LRegs::LReg0];
  vInt b = __builtin_rvtt_sfpcast(a.get(), SFPCAST_MOD1_SM32_TO_INT32);
  l_reg[LRegs::LReg1] = b;
}
// { dg-final { scan-assembler {\n_Z4sm2iv:\n\tSFPCAST	L1, L0, 2\n\tret\n} } }

void i2sm () {
  vInt a = l_reg[LRegs::LReg0];
  vInt b = __builtin_rvtt_sfpcast(a.get(), SFPCAST_MOD1_INT32_TO_SM32);
  l_reg[LRegs::LReg1] = b;
}
// { dg-final { scan-assembler {\n_Z4i2smv:\n\tSFPCAST	L1, L0, 3\n\tret\n} } }

void cond () {
    vUInt a = l_reg[LRegs::LReg0];
    vUInt b = l_reg[LRegs::LReg1];
    vInt c = l_reg[LRegs::LReg2];
    vFloat r;
    
    v_if(a == b) {
      r = int32_to_float (c, false);
    } v_else {
      r = int32_to_float (c, true);
    } v_endif;

    l_reg[LRegs::LReg0] = r;
}
// { dg-final { scan-assembler {\n_Z4condv:\n\tSFPIADD	L1, L0, 0, 6\n\tSFPSETCC	L1, 0, 6\n\tSFPCAST	L0, L2, 0\n\tSFPCOMPC\n\tSFPCAST	L0, L2, 1\n\tSFPENCC	3, 10\n\tret\n} } }
