// { dg-options "-mcpu=tt-wh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void f1() {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = a << 2;
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f1v:\n\tSFPSHFT	L0, L0, 2, 1\n\tSFPMOV	L3, L0, 2\n\tret\n} } }

void f1r() {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = a >> 2;
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z3f1rv:\n\tSFPSHFT	L0, L0, -2, 1\n\tSFPMOV	L3, L0, 2\n\tret\n} } }

void f3(int s) {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = a << s;
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f3i:\n\tslli	a5,a0,12\n\tli	a4,16773120\n\tand	a5,a5,a4\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tli	a3, 2046820353	# 7a000001\n\tlw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)\n\tadd	a5,a5,a3\n\tsw	a5, 0\(a4\)	# Op\(0x7a\) lv\(lr0\)  d\(lr0\)\n\tSFPMOV	L3, L0, 2\n\tret\n} } }

void f3r(int s) {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = a >> s;
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z3f3ri:\n\tneg	a5,a0\n\tli	a4,16773120\n\tslli	a5,a5,12\n\tand	a5,a5,a4\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tli	a3, 2046820353	# 7a000001\n\tlw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)\n\tadd	a5,a5,a3\n\tsw	a5, 0\(a4\)	# Op\(0x7a\) lv\(lr0\)  d\(lr0\)\n\tSFPMOV	L3, L0, 2\n\tret\n} } }
