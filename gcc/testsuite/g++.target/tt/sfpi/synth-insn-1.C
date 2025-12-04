// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

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
/*
**_Z3onei:
**	li	a5,16773120
**	slli	a0,a0,12
**	and	a0,a0,a5
**	li	a5, 2046820407	# 2:7a000037
**	add	a0,a0,a5
**	lui	a5,%hi\(_ZN7ckernel13instrn_bufferE\)
**	sw	a0, %lo\(_ZN7ckernel13instrn_bufferE\)\(a5\)	# 2:7a000037 L3 := L0
**	ret
*/

// GCC's RTL doesnt DTRT in this case, needs a lo_sum pass
void two(int s) {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = __builtin_rvtt_sfpshft_i (a.get(), s, SFPSHFT_MOD1_ARITHMETIC);
  r = __builtin_rvtt_sfpshft_i (r.get(), s, SFPSHFT_MOD1_ARITHMETIC);
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z3twoi:
**	li	a5,16773120
**	slli	a0,a0,12
**	and	a0,a0,a5
**	lui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)
**	li	a5, 2046820359	# 2:7a000007
**	add	a5,a0,a5
**	addi	a4,a4,%lo\(_ZN7ckernel13instrn_bufferE\)
**	sw	a5, 0\(a4\)	# 2:7a000007 L0 := L0
**	li	a5, 2046820407	# 4:7a000037
**	add	a0,a0,a5
**	sw	a0, 0\(a4\)	# 4:7a000037 L3 := L0
**	ret
*/
