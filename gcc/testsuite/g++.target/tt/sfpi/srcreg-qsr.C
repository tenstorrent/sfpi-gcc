// { dg-options "-mcpu=tt-bh-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

#define ARCH_QUASAR 1
namespace ckernel{
  extern unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void unpack () {
  UnpackSrcS src_reg;
  auto a = src_reg[0];
  auto b = a + src_reg[1];
  auto c = b + src_reg[2];
  auto d = c + src_reg[3];
  auto e = d + src_reg[4];
  auto f = e + src_reg[5];
  dst_reg[0] = f;
}
/*
**_Z6unpackv:
**	SFPLOAD	L1, 1026, 0, 7
**	SFPLOAD	L0, 1024, 0, 7
**	SFPADD	L0, L10, L0, L1, 0
**	SFPLOAD	L1, 1028, 0, 7
**	SFPADD	L0, L10, L0, L1, 0
**	SFPLOAD	L1, 1030, 0, 7
**	SFPADD	L0, L10, L0, L1, 0
**	SFPLOAD	L1, 1032, 0, 7
**	SFPADD	L0, L10, L0, L1, 0
**	SFPLOAD	L1, 1034, 0, 7
**	SFPADD	L0, L10, L0, L1, 0
**	SFPNOP
**	SFPSTORE	0, L0, 0, 7
**	ret
*/
void compute () {
  ComputeSrcS src_reg;
#pragma GCC unroll 0
  for (unsigned ix = 0; ix != 8; ix++) {
    auto a = src_reg[0];
    auto b = src_reg[1];
    dst_reg[0] = a * b;
    dst_reg++;
    src_reg++;
  }
}
/*
**_Z7computev:
**	li	a7, 1880154112	# 2:7010e000
**	li	a5,1040
**	lui	a6,%hi\(_ZN7ckernel13instrn_bufferE\)
**	li	a0, 1879105536	# 4:7000e000
**	li	a1,1056
**	add	a4,a0,a5
**	lw	a3,%lo\(_ZN7ckernel13instrn_bufferE\)\(a6\)
**	addi	a5,a5,2
**	add	a2,a5,a7
**	sw	a2, 0\(a3\)	# 2:7010e000 L1 :=
**	sw	a4, 0\(a3\)	# 4:7000e000 L0 :=
**	SFPMUL	L0, L0, L1, L9, 0
**	SFPNOP
**	SFPSTORE	0, L0, 0, 7
**	TTINCRWC	0, 2, 0, 0
**	bne	a5,a1,\.L[0-9]*
**	ret
*/

void unroll () {
  ComputeSrcS src_reg;
#pragma GCC unroll 8
  for (unsigned ix = 0; ix != 8; ix++) {
    auto a = src_reg[0];
    auto b = src_reg[1];
    dst_reg[0] = a * b;
    dst_reg++;
    src_reg++;
  }
}
/*
**_Z6unrollv:
**	SFPLOAD	L1, 1042, 0, 7
**	SFPLOAD	L0, 1040, 0, 7
**	TTREPLAY	0, 4, 1, 1
**	SFPMUL	L0, L0, L1, L9, 0
**	SFPNOP
**	SFPSTORE	0, L0, 0, 7
**	TTINCRWC	0, 2, 0, 0
**	SFPLOAD	L1, 1044, 0, 7
**	SFPLOAD	L0, 1042, 0, 7
**	TTREPLAY	0, 4, 0, 0
**	SFPLOAD	L1, 1046, 0, 7
**	SFPLOAD	L0, 1044, 0, 7
**	TTREPLAY	0, 4, 0, 0
**	SFPLOAD	L1, 1048, 0, 7
**	SFPLOAD	L0, 1046, 0, 7
**	TTREPLAY	0, 4, 0, 0
**	SFPLOAD	L1, 1050, 0, 7
**	SFPLOAD	L0, 1048, 0, 7
**	TTREPLAY	0, 4, 0, 0
**	SFPLOAD	L1, 1052, 0, 7
**	SFPLOAD	L0, 1050, 0, 7
**	TTREPLAY	0, 4, 0, 0
**	SFPLOAD	L1, 1054, 0, 7
**	SFPLOAD	L0, 1052, 0, 7
**	TTREPLAY	0, 4, 0, 0
**	SFPLOAD	L1, 1056, 0, 7
**	SFPLOAD	L0, 1054, 0, 7
**	TTREPLAY	0, 4, 0, 0
**	ret
*/
