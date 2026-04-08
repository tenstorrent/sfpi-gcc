// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void one (float f) {
  l_reg[LRegs::LReg0] = vFloat (f);
  l_reg[LRegs::LReg1] = vFloat (1.0f);
  l_reg[LRegs::LReg2] = vFloat (0.0f);
  l_reg[LRegs::LReg3] = vFloat (0.5f);
  l_reg[LRegs::LReg4] = vFloat (2.0f);
  l_reg[LRegs::LReg5] = vFloat (0x1.8p23f);
  l_reg[LRegs::LReg6] = vFloat (0x1.abcdefp0f);
}
/*
**_Z3onef:
**	slli	a5,a0,16
**	lui	a3,%hi\(_ZN7ckernel13instrn_bufferE\)
**	li	a4, 1895956480	# 2:71020000
**	lw	a3,%lo\(_ZN7ckernel13instrn_bufferE\)\(a3\)
**	srli	a5,a5,16
**	add	a5,a5,a4
**	sw	a5, 0\(a3\)	# 2:SFPLOADI	L0, a5, 2
**	srli	a0,a0,16
**	li	a5, 1896349696	# 4:71080000
**	add	a0,a0,a5
**	sw	a0, 0\(a3\)	# 4:SFPLOADI	L0, a0, 8	# LV:L0
**	# WRITE L0
**	SFPLOADI	L1, 16256, 0
**	# WRITE L1
**	SFPLOADI	L2, 0, 0
**	# WRITE L2
**	SFPLOADI	L3, 16128, 0
**	# WRITE L3
**	SFPLOADI	L4, 16384, 0
**	# WRITE L4
**	SFPLOADI	L5, 19264, 0
**	# WRITE L5
**	SFPLOADI	L6, 59128, 2
**	SFPLOADI	L6, 16341, 8	# LV:L6
**	# WRITE L6
**	ret
*/

void two (unsigned b) {
  l_reg[LRegs::LReg0] = vFloat (sFloat16b (b));
  l_reg[LRegs::LReg1] = vFloat (sFloat16b (1.0f));
  l_reg[LRegs::LReg2] = vFloat (sFloat16b (0x4000));
  l_reg[LRegs::LReg3] = vFloat (sFloat16b (0x4800));
  l_reg[LRegs::LReg4] = vFloat (sFloat16b (0x4b40));
}
/*
**_Z3twoj:
**	slli	a0,a0,16
**	lui	a4,%hi\(_ZN7ckernel13instrn_bufferE\)
**	li	a5, 1895825408	# 2:71000000
**	lw	a4,%lo\(_ZN7ckernel13instrn_bufferE\)\(a4\)
**	srli	a0,a0,16
**	add	a0,a0,a5
**	sw	a0, 0\(a4\)	# 2:SFPLOADI	L0, a0, 0
**	# WRITE L0
**	SFPLOADI	L1, 16256, 0
**	# WRITE L1
**	SFPLOADI	L2, 16384, 0
**	# WRITE L2
**	SFPLOADI	L3, 18432, 0
**	# WRITE L3
**	SFPLOADI	L4, 19264, 0
**	# WRITE L4
**	ret
*/

void three (unsigned b) {
  l_reg[LRegs::LReg0] = vInt (1);
  l_reg[LRegs::LReg1] = vInt (0x00010000);
  l_reg[LRegs::LReg2] = vInt (0x40000000);
  l_reg[LRegs::LReg3] = vInt (0x7fffffff);
  l_reg[LRegs::LReg4] = vInt (0xffff8000);
  l_reg[LRegs::LReg5] = vInt (0x7fff);
  l_reg[LRegs::LReg6] = vInt (0x8000);
  l_reg[LRegs::LReg7] = vInt (-1);
}
/*
**_Z5threej:
**	SFPLOADI	L0, 1, 2
**	# WRITE L0
**	SFPLOADI	L1, 1, 0
**	# WRITE L1
**	SFPLOADI	L2, 16384, 0
**	# WRITE L2
**	SFPLOADI	L3, 65535, 2
**	SFPLOADI	L3, 32767, 8	# LV:L3
**	# WRITE L3
**	SFPLOADI	L4, 32768, 4
**	# WRITE L4
**	SFPLOADI	L5, 32767, 2
**	# WRITE L5
**	SFPLOADI	L6, 32768, 2
**	# WRITE L6
**	SFPLOADI	L7, 65535, 4
**	# WRITE L7
**	ret
*/

void four ()
{
#pragma GCC unroll 2
  for (unsigned ix = 0; ix != 2; ix++) {
    vInt a = l_reg[LRegs::LReg0];
    v_if (a < 0x7fffffff + ix) {
      a = 0;
    } v_endif;
    l_reg[LRegs::LReg1] = a;
  }
}
/*
**_Z4fourv:
**	# READ L0
**	SFPLOADI	L1, 65535, 2
**	SFPLOADI	L1, 32767, 8	# LV:L1
**	TTREPLAY	0, 4, 1, 1
**	SFPIADD	L1, L0, 0, 2
**	SFPLOADI	L0, 0, 0	# LV:L0
**	SFPMOV	L1, L0, 2
**	SFPENCC	3, 10
**	# WRITE L1
**	# READ L0
**	SFPLOADI	L1, 32768, 0
**	TTREPLAY	0, 4, 0, 0
**	# WRITE L1
**	ret
*/
