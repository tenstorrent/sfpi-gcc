// { dg-options "-mcpu=tt-qsr32-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void one () {
  ComputeSrcS src_reg;
  vFloat a = src_reg[0];
  vFloat b = src_reg[2].mode<SFPLOAD_MOD0_FMT_FP32> (2);

  src_reg++;

  src_reg[4] = a;
  src_reg[5].mode<SFPLOAD_MOD0_FMT_FP16B> () = b;
}
/*
**_Z3onev:
**	SFPLOAD	L1, 16, 0, 7, 1, 0
**	SFPLOAD	L0, 20, 3, 2, 1, 0
**	SFPSTORE	L1, 26, 0, 7, 1, 0
**	SFPSTORE	L0, 28, 2, 7, 1, 0
**	ret
*/

void two () {
  ComputeSrcS src_reg;
  vInt a = src_reg[0];
  vUInt b = src_reg[1];

  src_reg[2] = a;
  src_reg[3].done () = b;
}
/*
**_Z3twov:
**	SFPLOAD	L1, 16, 4, 7, 1, 0
**	SFPLOAD	L0, 18, 4, 7, 1, 0
**	SFPSTORE	L1, 20, 4, 7, 1, 0
**	SFPSTORE	L0, 22, 4, 7, 1, 1
**	ret
*/
