// { dg-options "-mcpu=tt-wh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel {
  extern unsigned instrn_buffer[];
}

#include <lltt.h>
#include <sfpi.h>

void foo (uint32_t v) {
  lltt::insn<false&&true>(0x12345678);
  lltt::insn<true>(v);
}
/*
**_Z3foom:
**	.ttinsn	305419896
**	lui	a5,%hi\(__instrn_buffer\)
**	sw	a0,%lo\(__instrn_buffer\)\(a5\)
**	ret
*/

using namespace sfpi;

void foo () {
  vFloat c = l_reg[LRegs::LReg2];
  vFloat d = c + c;
  // needs NOP
  vFloat e = d + d;
  l_reg[LRegs::LReg0] = e;
}
/*
**_Z3foov:
**	SFPADD	L2, L10, L2, L2, 0
**	SFPNOP
**	SFPADD	L0, L10, L2, L2, 0
**	ret
*/

void bar () {
  vFloat c = l_reg[LRegs::LReg2];
  vFloat d = c + c;
  lltt::insn(0x12345678);
  // no need for NOP
  vFloat e = d + d;
  l_reg[LRegs::LReg0] = e;
}
/*
**_Z3barv:
**	SFPADD	L2, L10, L2, L2, 0
**	SFPNOP
**	.ttinsn	305419896
**	SFPADD	L0, L10, L2, L2, 0
**	ret
*/

void baz (uint32_t v) {
  vFloat c = l_reg[LRegs::LReg2];
  vFloat d = c + c;
  lltt::insn<true>(v);
  // no need for NOP
  vFloat e = d + d;
  l_reg[LRegs::LReg0] = e;
}
/*
**_Z3bazm:
**	SFPADD	L2, L10, L2, L2, 0
**	SFPNOP
**	lui	a5,%hi\(__instrn_buffer\)
**	sw	a0,%lo\(__instrn_buffer\)\(a5\)
**	SFPADD	L0, L10, L2, L2, 0
**	ret
*/
