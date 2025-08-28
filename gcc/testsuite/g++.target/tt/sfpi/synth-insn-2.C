// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }

namespace ckernel {
extern volatile unsigned instrn_buffer[];
}
#include "sfpi.h"

using namespace sfpi;

void loop_diff (int i) {
  vUInt a = l_reg[LRegs::LReg0];

#pragma GCC unroll 4
  for (int ix = 0; ix < 4; ix++) {
    a = __builtin_rvtt_sfpshft_i (a.get(), i + ix, SFPSHFT_MOD1_LOGICAL);
  }

  l_reg[LRegs::LReg3] = a;
}
// { dg-final { scan-assembler {\n_Z9loop_diffi:\n\tslli	a5,a0,12\n\tli	a3,16773120\n\tand	a5,a5,a3\n\tli	a4, 2046820357	# 4:7a000005\n\tadd	a5,a5,a4\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\taddi	a4,a4,%lo\(_ZN7ckernel13instrn_bufferE\)\n\tsw	a5, 0\(a4\)	# 4:7a000005 L0 := L0\n\taddi	a5,a0,1\n\tslli	a5,a5,12\n\tand	a5,a5,a3\n\tli	a2, 2046820405	# 2:7a000035\n\tadd	a5,a5,a2\n\tsw	a5, 0\(a4\)	# 2:7a000035 L3 := L0\n\taddi	a5,a0,2\n\tslli	a5,a5,12\n\tand	a5,a5,a3\n\tli	a2, 2046821125	# 8:7a000305\n\tadd	a5,a5,a2\n\tsw	a5, 0\(a4\)	# 8:7a000305 L0 := L3\n\taddi	a5,a0,3\n\tslli	a5,a5,12\n\tand	a5,a5,a3\n\tli	a3, 2046820405	# 6:7a000035\n\tadd	a5,a5,a3\n\tsw	a5, 0\(a4\)	# 6:7a000035 L3 := L0\n\tret\n} }  }

void loop_common (int i) {
  vUInt a = l_reg[LRegs::LReg0];

#pragma GCC unroll 4
  for (int ix = 0; ix < 4; ix++) {
    a = __builtin_rvtt_sfpshft_i (a.get(), i, SFPSHFT_MOD1_LOGICAL);
  }

  l_reg[LRegs::LReg3] = a;
}
// { dg-final { scan-assembler {\n_Z11loop_commoni:\n\tslli	a5,a0,12\n\tli	a4,16773120\n\tand	a5,a5,a4\n\tli	a4, 2046820405	# 2:7a000035\n\tadd	a5,a5,a4\n\tlui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)\n\taddi	a4,a4,%lo\(_ZN7ckernel13instrn_bufferE\)\n\tli	a3,48\n\txor	a3,a3,a5\n\tsw	a3, 0\(a4\)	# 2:7a000005 L0 := L0\n\tsw	a5, 0\(a4\)	# 2:7a000035 L3 := L0\n\tli	a3,816\n\txor	a3,a3,a5\n\tsw	a3, 0\(a4\)	# 2:7a000305 L0 := L3\n\tsw	a5, 0\(a4\)	# 2:7a000035 L3 := L0\n\tret\n} } }
