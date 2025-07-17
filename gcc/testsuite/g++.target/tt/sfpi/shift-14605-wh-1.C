// { dg-options "-mcpu=tt-wh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void f1() {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = __builtin_rvtt_sfpshft_i (a.get(), 2);
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f1v:\n\tSFPSHFT	L0, L0, 2, 1\n\tSFPMOV	L3, L0, 2\n\tret\n} } }

void f3(int s) {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = __builtin_rvtt_sfpshft_i (a.get(), s);
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f3i:\n\tli	a5,16773120\n\tslli	a0,a0,12\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tand	a0,a0,a5\n\tlw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)\n\tli	a5, 2046820353	# 2:7a000001\n\tadd	a0,a0,a5\n\tsw	a0, 0\(a4\)	# 2:7a000001 L0 := LV\n\tSFPMOV	L3, L0, 2\n\tret\n} } }

void f5() {
  vUInt a = l_reg[LRegs::LReg0];
  vUInt b = l_reg[LRegs::LReg1];

  vUInt r = __builtin_rvtt_sfpshft_v (a.get(), b.get());
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f5v:\n\tSFPSHFT	L0, L1, 0, 0\n\tSFPMOV	L3, L0, 2\n\tret\n} } }
