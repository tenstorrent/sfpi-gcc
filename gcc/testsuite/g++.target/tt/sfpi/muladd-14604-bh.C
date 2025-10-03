// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void muladd() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  vFloat r;

  r = b * c + a;
  
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z6muladdv:
**	SFPMAD	L3, L1, L2, L0, 0
**	ret
*/

void mulsub() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  vFloat r;

  r = b * c - a;
  
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z6mulsubv:
**	SFPMAD	L3, L1, L2, L0, 2
**	ret
*/

void negmuladd() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  vFloat r;

  r = b * -c + a;
  
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z9negmuladdv:
**	SFPMAD	L3, L1, L2, L0, 1
**	ret
*/

void negmulsub() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  vFloat r;

  r = b * -c - a;
  
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z9negmulsubv:
**	SFPMAD	L3, L1, L2, L0, 3
**	ret
*/

void negmuladd2() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  vFloat r;

  r = -b * c + a;
  
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z10negmuladd2v:
**	SFPMAD	L3, L1, L2, L0, 1
**	ret
*/

void negmulsub2() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  vFloat r;

  r = -b * c - a;
  
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z10negmulsub2v:
**	SFPMAD	L3, L1, L2, L0, 3
**	ret
*/
