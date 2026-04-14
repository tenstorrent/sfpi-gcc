// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void one () {
  vFloat a = dst_reg[0];
  vFloat b = dst_reg[2].mode<SFPLOAD_MOD0_FMT_FP32> (2);

  dst_reg[4] = a;
  dst_reg[5].mode<SFPLOAD_MOD0_FMT_FP16B> () = b;
}
/*
**_Z3onev:
**	SFPLOAD	L1, 0, 0, 3
**	SFPLOAD	L0, 4, 3, 2
**	SFPSTORE	L1, 8, 0, 3
**	SFPSTORE	L0, 10, 2, 3
**	ret
*/

void two () {
  vInt a = dst_reg[0];
  vUInt b = dst_reg[1];

  dst_reg[2] = a;
  dst_reg[3] = b;
}
/*
**_Z3onev:
**	SFPLOAD	L1, 0, 0, 3
**	SFPLOAD	L0, 4, 3, 2
**	SFPSTORE	L1, 8, 0, 3
**	SFPSTORE	L0, 10, 2, 3
**	ret
*/
