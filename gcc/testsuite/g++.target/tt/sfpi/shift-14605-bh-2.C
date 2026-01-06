// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

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
/*
**_Z2f1v:
**	SFPSHFT	L3, L0, 2, 0 \| 5
**	ret
*/

void f1r() {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = a >> 2;
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z3f1rv:
**	SFPSHFT	L3, L0, -2, 0 \| 5
**	ret
*/

void f2() {
  vInt a = l_reg[LRegs::LReg0];

  vInt r = a << 2;
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z2f2v:
**	SFPSHFT	L3, L0, 2, 2 \| 5
**	ret
*/

void f2r() {
  vInt a = l_reg[LRegs::LReg0];

  vInt r = a >> 2;
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z3f2rv:
**	SFPSHFT	L3, L0, -2, 2 \| 5
**	ret
*/

void f3(int s) {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = a << s;
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z2f3i:
**	slli	a0,a0,12
**	li	a5,16773120
**	lui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)
**	and	a0,a0,a5
**	lw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)
**	li	a5, 2046820405	# 2:7a000035
**	add	a0,a0,a5
**	sw	a0, 0\(a4\)	# 2:7a000035 L3 := L0
**	ret
*/

void f3r(int s) {
  vUInt a = l_reg[LRegs::LReg0];

  vUInt r = a >> s;
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z3f3ri:
**	neg	a5,a0
**	li	a4,16773120
**	slli	a5,a5,12
**	lui	a3,%hi\(_ZN7ckernel13instrn_bufferE\)
**	and	a5,a5,a4
**	lw	a3,%lo\(_ZN7ckernel13instrn_bufferE\)\(a3\)
**	li	a4, 2046820405	# 2:7a000035
**	add	a5,a5,a4
**	sw	a5, 0\(a3\)	# 2:7a000035 L3 := L0
**	ret
*/

void f4(int s) {
  vInt a = l_reg[LRegs::LReg0];

  vInt r = a << s;
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z2f4i:
**	slli	a0,a0,12
**	li	a5,16773120
**	lui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)
**	and	a0,a0,a5
**	lw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)
**	li	a5, 2046820407	# 2:7a000037
**	add	a0,a0,a5
**	sw	a0, 0\(a4\)	# 2:7a000037 L3 := L0
**	ret
*/

void f4r(int s) {
  vInt a = l_reg[LRegs::LReg0];

  vInt r = a >> s;
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z3f4ri:
**	neg	a5,a0
**	li	a4,16773120
**	slli	a5,a5,12
**	lui	a3,%hi\(_ZN7ckernel13instrn_bufferE\)
**	and	a5,a5,a4
**	lw	a3,%lo\(_ZN7ckernel13instrn_bufferE\)\(a3\)
**	li	a4, 2046820407	# 2:7a000037
**	add	a5,a5,a4
**	sw	a5, 0\(a3\)	# 2:7a000037 L3 := L0
**	ret
*/
