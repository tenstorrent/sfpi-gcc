// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
  extern volatile unsigned instrn_buffer[];
}
#include <sfpi.h>

using namespace sfpi;

void xiaddxiadd () {
  vInt a = l_reg[LRegs::LReg0];

  auto b = vInt ((__builtin_rvtt_sfpxiadd_i) (0, a.get (), 5, 0, 0, SFPXIADD_MOD1_IS_SUB));
  v_if (b < 0) {
    b = 0;
  } v_endif;

  l_reg[LRegs::LReg0] = b;
}
/*
**_Z10xiaddxiaddv:
**	# READ L0
**	SFPIADD	L0, L0, -5, 1
**	SFPMOV	L0, L9, 0	# LV:L0
**	SFPENCC	3, 10
**	# WRITE L0
**	ret
*/

void exexpxiadd () {
  vFloat a = l_reg[LRegs::LReg0];

  auto b = exexp (a);
  v_if (b < 0) {
    b = 0;
  } v_endif;

  l_reg[LRegs::LReg0] = b;
}
/*
**_Z10exexpxiaddv:
**	# READ L0
**	SFPEXEXP	L0, L0, 2
**	SFPMOV	L0, L9, 0	# LV:L0
**	SFPENCC	3, 10
**	# WRITE L0
**	ret
*/

