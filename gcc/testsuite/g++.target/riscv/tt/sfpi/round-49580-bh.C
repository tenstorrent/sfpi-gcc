// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
  extern volatile unsigned instrn_buffer[];
}
#include <sfpi.h>

using namespace sfpi;

void round1 () {
  vFloat x = l_reg[LRegs::LReg0];

  vFloat r = round (x);
  l_reg[LRegs::LReg0] = r;
}
/*
**_Z6round1v:
**	# READ L0
**	SFPLOADI	L1, 19264, 0
**	SFPADDI	L0, 19264, 0
**	SFPADD	L0, L1, L0, 1
**	# WRITE L0
**	ret
*/

void round2 () {
  vFloat x = l_reg[LRegs::LReg0];

  vInt r = round (x);
  l_reg[LRegs::LReg1] = r;
}
/*
**_Z6round2v:
**	# READ L0
**	SFPLOADI	L1, 19264, 0
**	SFPADDI	L0, 19264, 0
**	SFPNOP
**	SFPIADD	L1, L0, 0, 6
**	# WRITE L1
**	ret
*/

void round3 () {
  vFloat x = l_reg[LRegs::LReg0];

  auto [f, i] = round (x);
  l_reg[LRegs::LReg0] = f;
  l_reg[LRegs::LReg1] = i;
}
/*
**_Z6round3v:
**	# READ L0
**	SFPLOADI	L1, 19264, 0
**	SFPADDI	L0, 19264, 0
**	SFPMOV	L2, L0, 2
**	SFPADD	L0, L1, L0, 1
**	SFPIADD	L1, L2, 0, 6
**	# WRITE L0
**	# WRITE L1
**	ret
*/

