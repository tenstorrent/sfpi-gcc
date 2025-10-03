// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

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
/*
**_Z9loop_diffi:
**	slli	a5,a0,12
**	li	a3,16773120
**	and	a5,a5,a3
**	li	a4, 2046820405	# 4:7a000035
**	add	a5,a5,a4
**	lui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)
**	addi	a4,a4,%lo\(_ZN7ckernel13instrn_bufferE\)
**	li	a2, 2046821173	# 2:7a000335
**	sw	a5, 0\(a4\)	# 4:7a000035 L3 := L0
**	addi	a5,a0,1
**	slli	a5,a5,12
**	and	a5,a5,a3
**	add	a5,a5,a2
**	sw	a5, 0\(a4\)	# 2:7a000335 L3 := L3
**	addi	a5,a0,2
**	slli	a5,a5,12
**	and	a5,a5,a3
**	li	a2, 2046821173	# 8:7a000335
**	add	a5,a5,a2
**	sw	a5, 0\(a4\)	# 8:7a000335 L3 := L3
**	addi	a5,a0,3
**	slli	a5,a5,12
**	and	a5,a5,a3
**	li	a3, 2046821173	# 6:7a000335
**	add	a5,a5,a3
**	sw	a5, 0\(a4\)	# 6:7a000335 L3 := L3
**	ret
*/

void loop_common (int i) {
  vUInt a = l_reg[LRegs::LReg0];

#pragma GCC unroll 4
  for (int ix = 0; ix < 4; ix++) {
    a = __builtin_rvtt_sfpshft_i (a.get(), i, SFPSHFT_MOD1_LOGICAL);
  }

  l_reg[LRegs::LReg3] = a;
}
/*
**_Z11loop_commoni:
**	slli	a5,a0,12
**	li	a4,16773120
**	and	a5,a5,a4
**	li	a3, 2046821173	# 2:7a000335
**	lui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)
**	add	a5,a5,a3
**	addi	a4,a4,%lo\(_ZN7ckernel13instrn_bufferE\)
**	li	a3,768
**	xor	a3,a3,a5
**	sw	a3, 0\(a4\)	# 2:7a000035 L3 := L0
**	sw	a5, 0\(a4\)	# 2:7a000335 L3 := L3
**	sw	a5, 0\(a4\)	# 2:7a000335 L3 := L3
**	sw	a5, 0\(a4\)	# 2:7a000335 L3 := L3
**	ret
*/
