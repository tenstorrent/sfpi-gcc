// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

#include <cstdint>
namespace ckernel{
  extern volatile uint32_t instrn_buffer[];
}
#include <sfpi.h>

void frob () {
  sfpi::vFloat tmp = sfpi::vConstFloatPrgm0;
  sfpi::dst_reg[0] = tmp;
}
/*
**_Z4frobv:
**	SFPMOV	L0, L12, 2
**	SFPSTORE	0, L0, 0, 7
**	ret
*/

void frob (int i) {
  sfpi::vFloat tmp = sfpi::vConstFloatPrgm0;
  sfpi::dst_reg[i] = tmp;
}
/*
**_Z4frobi:
**	li	a5,8192
**	addi	a5,a5,-1
**	slli	a0,a0,1
**	and	a0,a0,a5
**	li	a5, 1912659968	# 2:7200e000
**	add	a0,a0,a5
**	SFPMOV	L0, L12, 2
**	lui	a5,%hi\(_ZN7ckernel13instrn_bufferE\)
**	sw	a0, %lo\(_ZN7ckernel13instrn_bufferE\)\(a5\)	# 2:7200e000 L0
**	ret
*/
