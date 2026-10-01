// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -Wall -Wextra -Wunused-parameter" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    extern unsigned long instrn_buffer[];
}
#include <sfpi.h>

using namespace sfpi;

void one () {
  vFloat a = l_reg[LRegs::LReg0];

  a = a * -vFloat (2.0f);
  l_reg[LRegs::LReg0] = a;

  a = a + - vFloat (2.0f);
  l_reg[LRegs::LReg0] = a;
}
/*
**_Z3onev:
**	# READ L0
**	SFPMULI	L0, 49152, 0
**	# WRITE L0
**	SFPADDI	L0, 49152, 0
**	# WRITE L0
**	ret
*/

void two () {
  vFloat a = l_reg[LRegs::LReg0];

  // These are ordinary multiply-plus-add expressions, not fused-FMA API
  // calls.  Keep their two-rounding MULI/ADDI lowering; it avoids two
  // constant loads without changing the source expression's semantics.
  a = a * -vFloat (2.0f) + -vFloat (2.0f);
  l_reg[LRegs::LReg0] = a;

  {
    lreg_pressure _;
    a = a * -vFloat (2.0f) + -vFloat (2.0f);
    l_reg[LRegs::LReg0] = a;
  }

  a = a * -vFloat (2.0f) + -vFloat (2.0f);
  l_reg[LRegs::LReg0] = a;
}
/*
**_Z3twov:
**	# READ L0
**	SFPMULI	L0, 49152, 0
**	SFPADDI	L0, 49152, 0
**	# WRITE L0
**	SFPMULI	L0, 49152, 0
**	SFPADDI	L0, 49152, 0
**	# WRITE L0
**	SFPMULI	L0, 49152, 0
**	SFPADDI	L0, 49152, 0
**	# WRITE L0
**	ret
*/
