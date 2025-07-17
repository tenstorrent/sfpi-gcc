// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void muladd() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  vFloat r;

  r = b * c + a;
  
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z6muladdv:\n\tSFPMAD	L3, L1, L2, L0, 0\n\tret\n} } }

void mulsub() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  vFloat r;

  r = b * c - a;
  
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z6mulsubv:\n\tSFPMAD	L3, L1, L2, L0, 2\n\tret\n} } }

void negmuladd() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  vFloat r;

  r = b * -c + a;
  
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z9negmuladdv:\n\tSFPMAD	L3, L1, L2, L0, 1\n\tret\n} } }

void negmulsub() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  vFloat r;

  r = b * -c - a;
  
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z9negmulsubv:\n\tSFPMAD	L3, L1, L2, L0, 3\n\tret\n} } }

void negmuladd2() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  vFloat r;

  r = -b * c + a;
  
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z10negmuladd2v:\n\tSFPMAD	L3, L1, L2, L0, 1\n\tret\n} } }

void negmulsub2() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  vFloat r;

  r = -b * c - a;
  
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z10negmulsub2v:\n\tSFPMAD	L3, L1, L2, L0, 3\n\tret\n} } }
