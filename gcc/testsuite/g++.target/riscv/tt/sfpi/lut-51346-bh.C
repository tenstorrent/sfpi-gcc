// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void LUT () {
  vFloat v = dst_reg[0];
  sFloat8Pair a0 (1.0f, 2.0f);
  sFloat8Pair a1 (3.0f, 4.0f);
  sFloat8Pair a2 (5.0f, 6.0f);

  v = lut (v, a0, a1, a2);
  dst_reg[0] = v;
}
/*
**_Z3LUTv:
**	SFPLOAD	L3, 0, 0, 7
**	SFPLOADI	L0, 240, 2
**	SFPLOADI	L1, 61664, 2
**	SFPLOADI	L2, 57568, 2
**	SFPLUT	L3, 4	# R:L0,L1,L2,L3
**	SFPSTORE	L3, 0, 0, 7
**	ret
*/

void LUT_SIGN () {
  vFloat v = dst_reg[0];
  sFloat8Pair a0 (1.0f, 2.0f);
  sFloat8Pair a1 (3.0f, 4.0f);
  sFloat8Pair a2 (5.0f, 6.0f);

  v = lut (v, a0, a1, a2, LutSign::Update);
  dst_reg[0] = v;
}
/*
**_Z8LUT_SIGNv:
**	SFPLOAD	L3, 0, 0, 7
**	SFPLOADI	L0, 240, 2
**	SFPLOADI	L1, 61664, 2
**	SFPLOADI	L2, 57568, 2
**	SFPLUT	L3, 0	# R:L0,L1,L2,L3
**	SFPSTORE	L3, 0, 0, 7
**	ret
*/

void LUT2_FP32 () {
  vFloat v = dst_reg[0];
  vFloat a0 = dst_reg[1];
  vFloat b0 = dst_reg[2];
  vFloat a1 = dst_reg[3];
  vFloat b1 = dst_reg[4];
  vFloat a2 = dst_reg[5];
  vFloat b2 = dst_reg[6];

  v = lut (v, a0, a1, a2, b0, b1, b2);
  dst_reg[0] = v;
}
/*
**_Z9LUT2_FP32v:
**	SFPLOAD	L3, 0, 0, 7
**	SFPLOAD	L0, 2, 0, 7
**	SFPLOAD	L4, 4, 0, 7
**	SFPLOAD	L1, 6, 0, 7
**	SFPLOAD	L5, 8, 0, 7
**	SFPLOAD	L2, 10, 0, 7
**	SFPLOAD	L6, 12, 0, 7
**	SFPLUTFP32	L0, 4	# R:L0,L1,L2,L4,L5,L6,L3
**	SFPSTORE	L0, 0, 0, 7
**	ret
*/

void LUT2_FP16_3 () {
  vFloat v = dst_reg[0];
  vFloat16bPair a0 (1.0f, 2.0f);
  vFloat16bPair a1 (3.0f, 4.0f);
  vFloat16bPair a2 (5.0f, 6.0f);

  v = lut (v, a0, a1, a2);
  dst_reg[0] = v;
}
/*
**_Z11LUT2_FP16_3v:
**	SFPLOAD	L3, 0, 0, 7
**	SFPLOADI	L0, 15362, 1
**	SFPLOADI	L1, 16512, 2
**	SFPLOADI	L1, 16448, 8	# LV:L1
**	SFPLOADI	L2, 16576, 2
**	SFPLOADI	L2, 16544, 8	# LV:L2
**	SFPLOADI	L7, 0, 2
**	SFPLUTFP32	L0, 14	# R:L0,L1,L2,L3,L7
**	SFPSTORE	L0, 0, 0, 7
**	ret
*/

void LUT2_FP16_6a () {
  vFloat v = dst_reg[0];
  vFloat16bPair a0 (1.0f, 2.0f);
  vFloat16bPair a1 (3.0f, 4.0f);
  vFloat16bPair a2 (5.0f, 6.0f);
  vFloat16bPair b0 (11.0f, 12.0f);
  vFloat16bPair b1 (13.0f, 14.0f);
  vFloat16bPair b2 (15.0f, 16.0f);

  v = lut (v, a0, a1, a2, b0, b1, b2);
  dst_reg[0] = v;
}
/*
**_Z12LUT2_FP16_6av:
**	SFPLOAD	L3, 0, 0, 7
**	SFPLOADI	L0, 15362, 1
**	SFPLOADI	L1, 16512, 2
**	SFPLOADI	L1, 16448, 8	# LV:L1
**	SFPLOADI	L2, 16576, 2
**	SFPLOADI	L2, 16544, 8	# LV:L2
**	SFPLOADI	L4, 16704, 2
**	SFPLOADI	L4, 16688, 8	# LV:L4
**	SFPLOADI	L5, 16736, 2
**	SFPLOADI	L5, 16720, 8	# LV:L5
**	SFPLOADI	L6, 16768, 2
**	SFPLOADI	L6, 16752, 8	# LV:L6
**	SFPLUTFP32	L0, 6	# R:L0,L1,L2,L4,L5,L6,L3
**	SFPSTORE	L0, 0, 0, 7
**	ret
*/

void LUT2_FP16_6b () {
  vFloat v = dst_reg[0];
  vFloat16bPair a0 (1.0f, 2.0f);
  vFloat16bPair a1 (3.0f, 4.0f);
  vFloat16bPair a2 (5.0f, 6.0f);
  vFloat16bPair b0 (11.0f, 12.0f);
  vFloat16bPair b1 (13.0f, 14.0f);
  vFloat16bPair b2 (15.0f, 16.0f);

  v = lut<LutMode::Fp16x6_HWM4> (v, a0, a1, a2, b0, b1, b2);
  dst_reg[0] = v;
}
/*
**_Z12LUT2_FP16_6bv:
**	SFPLOAD	L3, 0, 0, 7
**	SFPLOADI	L0, 15362, 1
**	SFPLOADI	L1, 16512, 2
**	SFPLOADI	L1, 16448, 8	# LV:L1
**	SFPLOADI	L2, 16576, 2
**	SFPLOADI	L2, 16544, 8	# LV:L2
**	SFPLOADI	L4, 16704, 2
**	SFPLOADI	L4, 16688, 8	# LV:L4
**	SFPLOADI	L5, 16736, 2
**	SFPLOADI	L5, 16720, 8	# LV:L5
**	SFPLOADI	L6, 16768, 2
**	SFPLOADI	L6, 16752, 8	# LV:L6
**	SFPLUTFP32	L0, 7	# R:L0,L1,L2,L4,L5,L6,L3
**	SFPSTORE	L0, 0, 0, 7
**	ret
*/
