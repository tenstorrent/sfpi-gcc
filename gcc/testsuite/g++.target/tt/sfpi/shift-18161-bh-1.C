// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void f5() {
  vUInt a = l_reg[LRegs::LReg0];
  vInt b = l_reg[LRegs::LReg1];

  vUInt r = a << b;
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f5v:\n\tSFPSHFT	L0, L1, 0, 0\n\tSFPMOV	L3, L0, 2\n\tret\n} } }

void f5r() {
  vUInt a = l_reg[LRegs::LReg0];
  vInt b = l_reg[LRegs::LReg1];

  vUInt r = a >> b;
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z3f5rv:\n\tSFPLOADI	L2, 0, 4\n\tSFPIADD	L1, L2, 0, 6\n\tSFPSHFT	L0, L1, 0, 0\n\tSFPMOV	L3, L0, 2\n\tret\n} } }

void f6() {
  vInt a = l_reg[LRegs::LReg0];
  vInt b = l_reg[LRegs::LReg1];

  vInt r = a << b;
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f6v:\n\tSFPSHFT	L0, L1, 0, 2\n\tSFPMOV	L3, L0, 2\n\tret\n} } }

void f6r() {
  vInt a = l_reg[LRegs::LReg0];
  vInt b = l_reg[LRegs::LReg1];

  vInt r = a >> b;
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z3f6rv:\n\tSFPLOADI	L2, 0, 4\n\tSFPIADD	L1, L2, 0, 6\n\tSFPSHFT	L0, L1, 0, 2\n\tSFPMOV	L3, L0, 2\n\tret\n} } }
