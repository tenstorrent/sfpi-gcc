// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

namespace recip {
void foo () {
  // Basic test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat r = approx_recip (a);
  l_reg[LRegs::LReg1] = r;
}
/*
**_ZN5recip3fooEv:
**	SFPARECIP	L1, L0, 0
**	ret
*/

void bar () {
  // Live value test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  
  vFloat r;
  v_if (a < b) {
    r = c;
  } v_else {
    r = approx_recip (a);
  } v_endif;

  l_reg[LRegs::LReg3] = r;
}
/*
**_ZN5recip3barEv:
**	SFPMAD	L1, L1, L11, L0, 0
**	SFPNOP
**	SFPSETCC	L1, 0, 0
**	SFPCOMPC
**	SFPARECIP	L2, L0, 0
**	SFPMOV	L3, L2, 2
**	SFPENCC	3, 10
**	ret
*/
}

namespace negrecip {
void foo () {
  // Basic test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat r = approx_recip<false> (a);
  l_reg[LRegs::LReg1] = r;
}
/*
**_ZN8negrecip3fooEv:
**	SFPARECIP	L1, L0, 1
**	ret
*/

void bar () {
  // Live value test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  
  vFloat r;
  v_if (a < b) {
    r = c;
  } v_else {
    r = approx_recip<false> (a);
  } v_endif;

  l_reg[LRegs::LReg3] = r;
}
/*
**_ZN8negrecip3barEv:
**	SFPMAD	L1, L1, L11, L0, 0
**	SFPNOP
**	SFPSETCC	L1, 0, 0
**	SFPCOMPC
**	SFPARECIP	L2, L0, 1
**	SFPMOV	L3, L2, 2
**	SFPENCC	3, 10
**	ret
*/
}

namespace expon {
void foo () {
  // Basic test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat r = approx_exp (a);
  l_reg[LRegs::LReg1] = r;
}
/*
**_ZN5expon3fooEv:
**	SFPARECIP	L1, L0, 2
**	ret
*/

void bar () {
  // Live value test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg2];
  
  vFloat r;
  v_if (a < b) {
    r = c;
  } v_else {
    r = approx_exp (a);
  } v_endif;

  l_reg[LRegs::LReg3] = r;
}
/*
**_ZN5expon3barEv:
**	SFPMAD	L1, L1, L11, L0, 0
**	SFPNOP
**	SFPSETCC	L1, 0, 0
**	SFPCOMPC
**	SFPARECIP	L2, L0, 2
**	SFPMOV	L3, L2, 2
**	SFPENCC	3, 10
**	ret
*/
}
