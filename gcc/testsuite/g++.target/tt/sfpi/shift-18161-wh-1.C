// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void f5() {
  vUInt a = l_reg[LRegs::LReg0];
  vInt b = l_reg[LRegs::LReg1];

  vUInt r = a << b;
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z2f5v:
**	SFPSHFT	L0, L1, 0, 0
**	SFPMOV	L3, L0, 2
**	ret
*/

void f5r() {
  vUInt a = l_reg[LRegs::LReg0];
  vInt b = l_reg[LRegs::LReg1];

  vUInt r = a >> b;
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z3f5rv:
**	SFPLOADI	L2, 0, 4
**	SFPIADD	L1, L2, 0, 6
**	SFPSHFT	L0, L1, 0, 0
**	SFPMOV	L3, L0, 2
**	ret
*/

void f6() {
  vInt a = l_reg[LRegs::LReg0];
  vInt b = l_reg[LRegs::LReg1];

  vInt r = a << b;
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z2f6v:
**	SFPSHFT	L0, L1, 0, 0
**	SFPMOV	L3, L0, 2
**	ret
*/
