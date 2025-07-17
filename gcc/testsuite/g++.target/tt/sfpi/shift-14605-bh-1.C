// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void f1() {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = __builtin_rvtt_sfpshft_i (a.get(), 2, SFPSHFT_MOD1_LOGICAL);
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f1v:\n\tSFPSHFT	L3, L0, 2, 0 \| 5\n\tret\n} } }

void f2() {
  vInt a = l_reg[LRegs::LReg0];

  vInt r = __builtin_rvtt_sfpshft_i (a.get(), 2, SFPSHFT_MOD1_ARITHMETIC);
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f2v:\n\tSFPSHFT	L3, L0, 2, 2 \| 5\n\tret\n} } }

void f3(int s) {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = __builtin_rvtt_sfpshft_i (a.get(), s, SFPSHFT_MOD1_LOGICAL);
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f3i:\n\tslli	a5,a0,12\n\tli	a4,16773120\n\tand	a5,a5,a4\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tli	a3, 2046820405	# 2:7a000035\n\tlw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)\n\tadd	a5,a5,a3\n\tsw	a5, 0\(a4\)	# 2:7a000035 L3 := L0\n\tret\n} } }

void f4(int s) {
  vInt a = l_reg[LRegs::LReg0];

  vInt r = __builtin_rvtt_sfpshft_i (a.get(), s, SFPSHFT_MOD1_ARITHMETIC);
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f4i:\n\tslli	a5,a0,12\n\tli	a4,16773120\n\tand	a5,a5,a4\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tli	a3, 2046820407	# 2:7a000037\n\tlw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)\n\tadd	a5,a5,a3\n\tsw	a5, 0\(a4\)	# 2:7a000037 L3 := L0\n\tret\n} } }

void f5() {
  vUInt a = l_reg[LRegs::LReg0];
  vInt b = l_reg[LRegs::LReg1];

  vUInt r = __builtin_rvtt_sfpshft_v (a.get(), b.get(), SFPSHFT_MOD1_LOGICAL);
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f5v:\n\tSFPSHFT	L0, L1, 0, 0\n\tSFPMOV	L3, L0, 2\n\tret\n} } }

void f6() {
  vInt a = l_reg[LRegs::LReg0];
  vInt b = l_reg[LRegs::LReg1];

  vInt r = __builtin_rvtt_sfpshft_v (a.get(), b.get(), SFPSHFT_MOD1_ARITHMETIC);
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f6v:\n\tSFPSHFT	L0, L1, 0, 2\n\tSFPMOV	L3, L0, 2\n\tret\n} } }
