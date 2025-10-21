// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

// Verify we notice a - b is a + -1.0 * b

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void sub1() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat r;

  r = a - b;
  
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z4sub1v:
**	SFPADD	L3, L10, L0, L1, 2
**	ret
*/

void sub2() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];

  b -= a;
  
  l_reg[LRegs::LReg3] = b;
}
/*
**_Z4sub2v:
**	SFPADD	L3, L10, L1, L0, 2
**	ret
*/

void sub3() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat r;

  r = -a - b;
  
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z4sub3v:
**	SFPADD	L3, L10, L0, L1, 3
**	ret
*/

void sub4() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat r;

  v_if (a < b) {
    r = a - b;
  } v_else {
    r = b - a;
  } v_endif;
  
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z4sub4v:
**	SFPMAD	L2, L1, L11, L0, 0
**	SFPSETCC	L2, 0, 0
**	SFPADD	L3, L10, L0, L1, 2
**	SFPCOMPC
**	SFPADD	L3, L10, L1, L0, 2
**	SFPENCC	3, 10
**	ret
*/

void sub5() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];

  v_if (a < b) {
    b -= a;
  } v_else {
    b -= -a;
  } v_endif;
  
  l_reg[LRegs::LReg3] = b;
}
/*
**_Z4sub5v:
**	SFPMAD	L2, L1, L11, L0, 0
**	SFPSETCC	L2, 0, 0
**	SFPADD	L1, L10, L1, L0, 2
**	SFPCOMPC
**	SFPMOV	L0, L0, 1
**	SFPADD	L1, L10, L1, L0, 2
**	SFPNOP
**	SFPMOV	L3, L1, 2
**	SFPENCC	3, 10
**	ret
*/

void sub6() {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat r;

  v_if (a < b) {
    r = -a - b;
  } v_else {
    r = a - b;
  } v_endif;
  
  l_reg[LRegs::LReg3] = r;
}
/*
**_Z4sub6v:
**	SFPMAD	L2, L1, L11, L0, 0
**	SFPSETCC	L2, 0, 0
**	SFPADD	L3, L10, L0, L1, 3
**	SFPCOMPC
**	SFPADD	L3, L10, L0, L1, 2
**	SFPENCC	3, 10
**	ret
*/
