// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned long *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void empty () {
  v_block {
  } v_endblock;
}
/*
**_Z5emptyv:
**	SFPENCC	3, 10
**	ret
*/

void split (unsigned i) {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  v_if (a < b) {
    if (i & 1)
      {
	v_if (a < 5.0f) {
	  a = 6.0f;
	} v_endif;
      }
  } v_else {
    b = 5.0f;
  } v_endif;
  l_reg[LRegs::LReg0] = a;
  l_reg[LRegs::LReg1] = b;
}
/*
**_Z5splitj:
**	# READ L0
**	# READ L1
**	SFPGT	L1, L0, 0, 1
**	andi	a0,a0,1
**	beq	a0,zero,.L[0-9]+
**	SFPPUSHC	0
**	SFPLOADI	L2, 16544, 0
**	SFPGT	L2, L0, 0, 1
**	SFPLOADI	L0, 16576, 0	# LV:L0
**	SFPPOPC	0
**	SFPCOMPC
**	SFPLOADI	L1, 16544, 0	# LV:L1
**	SFPENCC	3, 10
**	# WRITE L0
**	# WRITE L1
**	ret
*/

void loop () {
  vFloat a = l_reg[LRegs::LReg0];
  vFloat b = l_reg[LRegs::LReg1];
  v_if (a < b) {
#pragma GCC unroll 0
    for (unsigned i = 5; i--;)
      v_and (a < sFloat16b (0x3f80 + (i << 4)));
  } v_else {
    b = 5.0f;
  } v_endif;
  l_reg[LRegs::LReg0] = a;
  l_reg[LRegs::LReg1] = b;
}
/*
**_Z4loopv:
**	# READ L0
**	# READ L1
**	SFPGT	L1, L0, 0, 1
**	li	a2,16384
**	addi	a5,a2,-64
**	li	a0, 1897922560	# 1:71200000
**	addi	a2,a2,-144
**	lui	a1,%hi\(_ZN7ckernel13instrn_bufferE\)
**	lw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a1\)
**	add	a3,a5,a0
**	sw	a3, 0\(a4\)	# 1:SFPLOADI	L2, a3, 0
**	SFPGT	L2, L0, 0, 1
**	addi	a5,a5,-16
**	bne	a5,a2,.L[0-9]+
**	SFPCOMPC
**	SFPLOADI	L1, 16544, 0	# LV:L1
**	SFPENCC	3, 10
**	# WRITE L0
**	# WRITE L1
**	ret
*/

