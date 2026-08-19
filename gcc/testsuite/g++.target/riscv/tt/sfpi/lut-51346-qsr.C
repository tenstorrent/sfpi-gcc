// { dg-options "-mcpu=tt-qsr32-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// { dg-final { check-function-bodies "**" "" } }

namespace ckernel{
    unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void LUT () {
  vFloat v = dst_reg[0];
  sFloat8Pair a0 (1.0f, 0.75f);
  sFloat8Pair a1 (0.0f, 2.0f);
  sFloat8Pair a2 (0.125f, 0.0625f);

  v = lut (v, a0, a1, a2);
  dst_reg[0] = v;
}
/*
**_Z3LUTv:
**	SFPLOAD	L3, 0, 0, 7, 0, 0
**	SFPLOADI	L0, 24, 2
**	SFPCONFIG	9, 0, 0	# R:L0 CFG:9
**	SFPLOADI	L0, 65295, 2
**	SFPCONFIG	10, 0, 0	# R:L0 CFG:10
**	SFPLOADI	L0, 12352, 2
**	SFPCONFIG	11, 0, 0	# R:L0 CFG:11
**	SFPLUT	L3, 4	# R:L3
**	SFPSTORE	L3, 0, 0, 7, 0, 0
**	ret
*/

void LUT_SIGN () {
  vFloat v = dst_reg[0];
  sFloat8Pair a0 (1.0f, 0.75f);
  sFloat8Pair a1 (0.50f, 0.25f);
  sFloat8Pair a2 (0.125f, 0.0625f);

  v = lut (v, a0, a1, a2, LutSign::Update);
  dst_reg[0] = v;
}
/*
**_Z8LUT_SIGNv:
**	SFPLOAD	L3, 0, 0, 7, 0, 0
**	SFPLOADI	L0, 24, 2
**	SFPCONFIG	9, 0, 0	# R:L0 CFG:9
**	SFPLOADI	L0, 4128, 2
**	SFPCONFIG	10, 0, 0	# R:L0 CFG:10
**	SFPLOADI	L0, 12352, 2
**	SFPCONFIG	11, 0, 0	# R:L0 CFG:11
**	SFPLUT	L3, 0	# R:L3
**	SFPSTORE	L3, 0, 0, 7, 0, 0
**	ret
*/

void LUT2_FP32 () {
  vFloat v = dst_reg[0];

  v = lut (v, 1.0f, 3.0f, 5.0f, 2.0f, 4.0f, 6.0f);
  dst_reg[0] = v;
}
/*
**_Z9LUT2_FP32v:
**	SFPLOAD	L3, 0, 0, 7, 0, 0
**	SFPLOADI	L0, 16256, 0
**	SFPCONFIG	9, 0, 0	# R:L0 CFG:9
**	SFPLOADI	L0, 16448, 0
**	SFPCONFIG	10, 0, 0	# R:L0 CFG:10
**	SFPLOADI	L0, 16544, 0
**	SFPCONFIG	11, 0, 0	# R:L0 CFG:11
**	SFPLOADI	L0, 16384, 0
**	SFPCONFIG	12, 0, 0	# R:L0 CFG:12
**	SFPLOADI	L0, 16512, 0
**	SFPCONFIG	13, 0, 0	# R:L0 CFG:13
**	SFPLOADI	L0, 16576, 0
**	SFPCONFIG	14, 0, 0	# R:L0 CFG:14
**	SFPLUTFP32	L3, 4	# R:L3
**	SFPSTORE	L3, 0, 0, 7, 0, 0
**	ret
*/

void LUT2_FP16_3 () {
  vFloat v = dst_reg[0];
  sFloat16bPair a0 (1.0f, 2.0f);
  sFloat16bPair a1 (3.0f, 4.0f);
  sFloat16bPair a2 (5.0f, 6.0f);

  v = lut (v, a0, a1, a2);
  dst_reg[0] = v;
}
/*
**_Z11LUT2_FP16_3v:
**	SFPLOAD	L3, 0, 0, 7, 0, 0
**	SFPLOADI	L0, 15362, 1
**	SFPCONFIG	9, 0, 0	# R:L0 CFG:9
**	SFPLOADI	L0, 16512, 2
**	SFPLOADI	L0, 16448, 8	# LV:L0
**	SFPCONFIG	10, 0, 0	# R:L0 CFG:10
**	SFPLOADI	L0, 16576, 2
**	SFPLOADI	L0, 16544, 8	# LV:L0
**	SFPCONFIG	11, 0, 0	# R:L0 CFG:11
**	SFPLUTFP32	L3, 14	# R:L3
**	SFPSTORE	L3, 0, 0, 7, 0, 0
**	ret
*/

void LUT2_FP16_6a () {
  vFloat v = dst_reg[0];
  sFloat16bPair a0 (1.0f, 2.0f);
  sFloat16bPair a1 (3.0f, 4.0f);
  sFloat16bPair a2 (5.0f, 6.0f);
  sFloat16bPair b0 (11.0f, 12.0f);
  sFloat16bPair b1 (13.0f, 14.0f);
  sFloat16bPair b2 (15.0f, 16.0f);

  v = lut (v, a0, a1, a2, b0, b1, b2);
  dst_reg[0] = v;
}
/*
**_Z12LUT2_FP16_6av:
**	SFPLOAD	L3, 0, 0, 7, 0, 0
**	SFPLOADI	L0, 15362, 1
**	SFPCONFIG	9, 0, 0	# R:L0 CFG:9
**	SFPLOADI	L0, 16512, 2
**	SFPLOADI	L0, 16448, 8	# LV:L0
**	SFPCONFIG	10, 0, 0	# R:L0 CFG:10
**	SFPLOADI	L0, 16576, 2
**	SFPLOADI	L0, 16544, 8	# LV:L0
**	SFPCONFIG	11, 0, 0	# R:L0 CFG:11
**	SFPLOADI	L0, 16704, 2
**	SFPLOADI	L0, 16688, 8	# LV:L0
**	SFPCONFIG	12, 0, 0	# R:L0 CFG:12
**	SFPLOADI	L0, 16736, 2
**	SFPLOADI	L0, 16720, 8	# LV:L0
**	SFPCONFIG	13, 0, 0	# R:L0 CFG:13
**	SFPLOADI	L0, 16768, 2
**	SFPLOADI	L0, 16752, 8	# LV:L0
**	SFPCONFIG	14, 0, 0	# R:L0 CFG:14
**	SFPLUTFP32	L3, 6	# R:L3
**	SFPSTORE	L3, 0, 0, 7, 0, 0
**	ret
*/

void LUT2_FP16_6b () {
  vFloat v = dst_reg[0];
  sFloat16bPair a0 (1.0f, 2.0f);
  sFloat16bPair a1 (3.0f, 4.0f);
  sFloat16bPair a2 (5.0f, 6.0f);
  sFloat16bPair b0 (11.0f, 12.0f);
  sFloat16bPair b1 (13.0f, 14.0f);
  sFloat16bPair b2 (15.0f, 16.0f);

  v = lut<LutMode::Fp16x6_HWM4> (v, a0, a1, a2, b0, b1, b2);
  dst_reg[0] = v;
}
/*
**_Z12LUT2_FP16_6bv:
**	SFPLOAD	L3, 0, 0, 7, 0, 0
**	SFPLOADI	L0, 15362, 1
**	SFPCONFIG	9, 0, 0	# R:L0 CFG:9
**	SFPLOADI	L0, 16512, 2
**	SFPLOADI	L0, 16448, 8	# LV:L0
**	SFPCONFIG	10, 0, 0	# R:L0 CFG:10
**	SFPLOADI	L0, 16576, 2
**	SFPLOADI	L0, 16544, 8	# LV:L0
**	SFPCONFIG	11, 0, 0	# R:L0 CFG:11
**	SFPLOADI	L0, 16704, 2
**	SFPLOADI	L0, 16688, 8	# LV:L0
**	SFPCONFIG	12, 0, 0	# R:L0 CFG:12
**	SFPLOADI	L0, 16736, 2
**	SFPLOADI	L0, 16720, 8	# LV:L0
**	SFPCONFIG	13, 0, 0	# R:L0 CFG:13
**	SFPLOADI	L0, 16768, 2
**	SFPLOADI	L0, 16752, 8	# LV:L0
**	SFPCONFIG	14, 0, 0	# R:L0 CFG:14
**	SFPLUTFP32	L3, 7	# R:L3
**	SFPSTORE	L3, 0, 0, 7, 0, 0
**	ret
*/
