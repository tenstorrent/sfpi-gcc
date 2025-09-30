// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void foo () {
  // Basic test
  vInt r = rand ();
  l_reg[LRegs::LReg0] = r;
}
/*
**_Z3foov:
**	SFPMOV	L0, L9, 8
**	ret
*/

void bar () {
  // Live value test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vInt c = l_reg[LRegs::LReg2];
  
  vInt r;
  v_if (a < b) {
    r = c;
  } v_else {
    r = rand ();
  } v_endif;
  
  l_reg[LRegs::LReg0] = r;
}
/*
**_Z3barv:
**	SFPMAD	L0, L1, L11, L0, 0
**	SFPNOP
**	SFPSETCC	L0, 0, 0
**	SFPCOMPC
**	SFPMOV	L0, L2, 2
**	SFPMOV	L0, L9, 8
**	SFPENCC	3, 10
**	ret
*/

void baz () {
  // Do not CSE
  vInt r = rand () + rand();
  l_reg[LRegs::LReg0] = r;
}
/*
**_Z3bazv:
**	SFPMOV	L1, L9, 8
**	SFPMOV	L0, L9, 8
**	SFPIADD	L0, L1, 0, 4
**	ret
*/
