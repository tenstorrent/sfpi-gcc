// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

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
// { dg-final { scan-assembler {\n_Z2f1v:\n\tSFPSHFT	L3, L0, 2, 0 \| 5\n\tret\n} } }

void f1r() {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = a >> 2;
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z3f1rv:\n\tSFPSHFT	L3, L0, -2, 0 \| 5\n\tret\n} } }

void f2() {
  vInt a = l_reg[LRegs::LReg0];

  vInt r = a << 2;
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f2v:\n\tSFPSHFT	L3, L0, 2, 2 \| 5\n\tret\n} } }

void f2r() {
  vInt a = l_reg[LRegs::LReg0];

  vInt r = a >> 2;
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z3f2rv:\n\tSFPSHFT	L3, L0, -2, 2 \| 5\n\tret\n} } }

void f3(int s) {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = a << s;
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f3i:\n\tli	a5,16773120\n\tslli	a0,a0,12\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tand	a0,a0,a5\n\tlw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)\n\tli	a5, 2046820405	# 2:7a000035\n\tadd	a0,a0,a5\n\tsw	a0, 0\(a4\)	# 2:7a000035 L3 := L0\n\tret\n} } }

void f3r(int s) {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = a >> s;
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z3f3ri:\n\tneg	a5,a0\n\tli	a4,16773120\n\tslli	a5,a5,12\n\tlui	a3,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tand	a5,a5,a4\n\tlw	a3,%lo\(_ZN7ckernel13instrn_bufferE\)\(a3\)\n\tli	a4, 2046820405	# 2:7a000035\n\tadd	a5,a5,a4\n\tsw	a5, 0\(a3\)	# 2:7a000035 L3 := L0\n\tret\n} } }

void f4(int s) {
  vInt a = l_reg[LRegs::LReg0];

  vInt r = a << s;
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z2f4i:\n\tli	a5,16773120\n\tslli	a0,a0,12\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tand	a0,a0,a5\n\tlw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)\n\tli	a5, 2046820407	# 2:7a000037\n\tadd	a0,a0,a5\n\tsw	a0, 0\(a4\)	# 2:7a000037 L3 := L0\n\tret\n} } }

void f4r(int s) {
  vInt a = l_reg[LRegs::LReg0];

  vInt r = a >> s;
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z3f4ri:\n\tneg	a5,a0\n\tli	a4,16773120\n\tslli	a5,a5,12\n\tlui	a3,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tand	a5,a5,a4\n\tlw	a3,%lo\(_ZN7ckernel13instrn_bufferE\)\(a3\)\n\tli	a4, 2046820407	# 2:7a000037\n\tadd	a5,a5,a4\n\tsw	a5, 0\(a3\)	# 2:7a000037 L3 := L0\n\tret\n} } }

