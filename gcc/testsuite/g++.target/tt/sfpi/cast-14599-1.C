// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
  extern unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void i2f_rne () {
  vInt a = l_reg[LRegs::LReg0];
  vFloat b = int32_to_float (a, false);
  l_reg[LRegs::LReg1] = b;
}
/*
**_Z7i2f_rnev:
**	SFPCAST	L1, L0, 0
**	ret
*/

void i2f_rns () {
  vInt a = l_reg[LRegs::LReg0];
  vFloat b = int32_to_float (a, true);
  l_reg[LRegs::LReg1] = b;
}
/*
**_Z7i2f_rnsv:
**	SFPCAST	L1, L0, 1
**	ret
*/

void sm2i () {
  vInt a = l_reg[LRegs::LReg0];
  vInt b = __builtin_rvtt_sfpcast(a.get(), SFPCAST_MOD1_SM32_TO_INT32);
  l_reg[LRegs::LReg1] = b;
}
/*
**_Z4sm2iv:
**	SFPCAST	L1, L0, 2
**	ret
*/

void i2sm () {
  vInt a = l_reg[LRegs::LReg0];
  vInt b = __builtin_rvtt_sfpcast(a.get(), SFPCAST_MOD1_INT32_TO_SM32);
  l_reg[LRegs::LReg1] = b;
}
/*
**_Z4i2smv:
**	SFPCAST	L1, L0, 3
**	ret
*/

void cond () {
    vUInt a = l_reg[LRegs::LReg0];
    vUInt b = l_reg[LRegs::LReg1];
    vInt c = l_reg[LRegs::LReg2];
    vFloat r;
    
    v_if(a == b) {
      r = int32_to_float (c, false);
    } v_else {
      r = int32_to_float (c, true);
    } v_endif;

    l_reg[LRegs::LReg0] = r;
}
/*
**_Z4condv:
**	SFPIADD	L1, L0, 0, 6
**	SFPSETCC	L1, 0, 6
**	SFPCAST	L0, L2, 0
**	SFPCOMPC
**	SFPCAST	L0, L2, 1
**	SFPENCC	3, 10
**	ret
*/
