// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel {
extern volatile unsigned instrn_buffer[];
}
#include "sfpi.h"

using namespace sfpi;

void one(int s) {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = __builtin_rvtt_sfpshft_i (a.get(), s, SFPSHFT_MOD1_ARITHMETIC);
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z3onei:\n\tslli	a5,a0,12\n\tli	a4,16773120\n\tand	a5,a5,a4\n\tli	a4, 2046820407	# 2:7a000037\n\tadd	a5,a5,a4\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tsw	a5, %lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)	# 2:7a000037 L3 := L0\n\tret\n} } }

// GCC's RTL doesnt DTRT in this case, needs a lo_sum pass
void two(int s) {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = __builtin_rvtt_sfpshft_i (a.get(), s, SFPSHFT_MOD1_ARITHMETIC);
  r = __builtin_rvtt_sfpshft_i (r.get(), s, SFPSHFT_MOD1_ARITHMETIC);
  l_reg[LRegs::LReg3] = r;
}
// { dg-final { scan-assembler {\n_Z3twoi:\n\tli	a5,16773120\n\tslli	a0,a0,12\n\tand	a0,a0,a5\n\tli	a4, 2046820359	# 2:7a000007\n\tlui	a5,%hi\(_ZN7ckernel13instrn_bufferE\)\n\tadd	a4,a0,a4\n\taddi	a5,a5,%lo\(_ZN7ckernel13instrn_bufferE\)\n\tsw	a4, 0\(a5\)	# 2:7a000007 L0 := L0\n\tli	a4, 2046820407	# 4:7a000037\n\tadd	a0,a0,a4\n\tsw	a0, 0\(a5\)	# 4:7a000037 L3 := L0\n\tret\n} } }
