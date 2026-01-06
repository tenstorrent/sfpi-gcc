// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

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
/*
**_Z2f1v:
**	SFPSHFT	L0, L0, 2, 1
**	SFPMOV	L3, L0, 2
**	ret
*/

void f3(int s) {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = __builtin_rvtt_sfpshft_i (a.get(), s);
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z2f3i:
**	slli	a0,a0,12
**	li	a5,16773120
**	lui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)
**	and	a0,a0,a5
**	lw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)
**	li	a5, 2046820353	# 2:7a000001
**	add	a0,a0,a5
**	sw	a0, 0\(a4\)	# 2:7a000001 L0 := LV
**	SFPMOV	L3, L0, 2
**	ret
*/

void f5() {
  vUInt a = l_reg[LRegs::LReg0];
  vUInt b = l_reg[LRegs::LReg1];

  vUInt r = __builtin_rvtt_sfpshft_v (a.get(), b.get());
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z2f5v:
**	SFPSHFT	L0, L1, 0, 0
**	SFPMOV	L3, L0, 2
**	ret
*/
