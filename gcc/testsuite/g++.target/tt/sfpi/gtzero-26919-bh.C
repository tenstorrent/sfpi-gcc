// { dg-options "-mcpu=tt-bh -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
extern unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void bad()
{
  vUInt i = l_reg[LRegs::LReg0];
  v_if (i > 0) {
  } v_endif;
}
/*
**_Z3badv:
**	SFPSETCC	L0, 0, 4
**	SFPSETCC	L0, 0, 2
**	SFPENCC	3, 10
**	ret
*/
