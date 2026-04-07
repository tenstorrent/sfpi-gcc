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
**	li	a4, 1896349696	# 3:71080000
**	sw	a5, 0\(a3\)	# 2:SFPLOADI	L0, a5, 2
**	srli	a0,a0,16
**	add	a0,a0,a4
**	sw	a0, 0\(a3\)	# 3:SFPLOADI	L0, a0, 8	# LV:L0
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
  l_reg[LRegs::LReg0] = vFloat (s2vFloat16b (b));
  l_reg[LRegs::LReg1] = vFloat (s2vFloat16b (1.0f));
  l_reg[LRegs::LReg2] = vFloat (s2vFloat16b (0x4000));
  l_reg[LRegs::LReg3] = vFloat (s2vFloat16b (0x4800));
  l_reg[LRegs::LReg4] = vFloat (s2vFloat16b (0x4b40));
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
