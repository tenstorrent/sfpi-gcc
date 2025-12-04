// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void foo () {
  // Basic test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat r_lo = __builtin_rvtt_sfpmul24 (a.get(), b.get(), SFPMUL24_MOD1_LOWER);
  vFloat r_hi = __builtin_rvtt_sfpmul24 (a.get(), b.get(), SFPMUL24_MOD1_UPPER);
  l_reg[LRegs::LReg2] = r_lo;
  l_reg[LRegs::LReg3] = r_hi;
}
/*
**_Z3foov:
**	SFPMUL24	L2, L0, L1, 0
**	SFPMUL24	L3, L0, L1, 1
**	ret
*/

void bar () {
  // Live value test
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  vFloat c = l_reg[LRegs::LReg4];
  vFloat d = l_reg[LRegs::LReg5];
  vFloat r_lo, r_hi;
  
  v_if (a < b) {
    r_lo = c;
    r_hi = d;
  } v_else {
    r_lo = __builtin_rvtt_sfpmul24 (a.get(), b.get(), SFPMUL24_MOD1_LOWER);
    r_hi = __builtin_rvtt_sfpmul24 (a.get(), b.get(), SFPMUL24_MOD1_UPPER);
  } v_endif;

  l_reg[LRegs::LReg2] = r_lo;
  l_reg[LRegs::LReg3] = r_hi;
}
/*
**_Z3barv:
**	SFPMAD	L2, L1, L11, L0, 0
**	SFPNOP
**	SFPSETCC	L2, 0, 0
**	SFPCOMPC
**	SFPMOV	L2, L4, 2
**	SFPMUL24	L2, L0, L1, 0
**	SFPMOV	L3, L5, 2
**	SFPMUL24	L3, L0, L1, 1
**	SFPENCC	3, 10
**	ret
*/
