// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-fp16 -fdump-tree-rvtt_lut_select" }
// { dg-final { check-function-bodies "**" "" } }
// Renamed production-shaped TABLE1 tree whose negative tiny intercepts need
// chained SFPLOADI + SFPLOADI_LV materializations before LUT formation.  Only
// the six synthesized packed coefficient words (two instructions each) may
// remain; the original chained roots must not survive as result-less row
// instructions after their tails and leaves are converted.
// { dg-final { scan-tree-dump-times "formed fp16-6entry-t1-sgn-update" 1 "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLOADI" 12 } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }

#define LUT_TREE_FN renamed_chained_coeff_cleanup
#define LUT_TREE_X sample
#define LUT_TREE_MAG magnitude
#define LUT_TREE_R approximation
#define LUT_TREE_TOP 3.0f
#define LUT_TREE_A0 0x1.58p-3f
#define LUT_TREE_B0 (-0x1.2fcp-15f)
#define LUT_TREE_A1 0x1.f68p-2f
#define LUT_TREE_B1 (-0x1.404p-3f)
#define LUT_TREE_A2 0x1.3cp-1f
#define LUT_TREE_B2 (-0x1.1cp-2f)
#define LUT_TREE_A3 0x1.384p-1f
#define LUT_TREE_B3 (-0x1.0ep-2f)
#define LUT_TREE_A4 0x1.158p-1f
#define LUT_TREE_B4 (-0x1p-3f)
#define LUT_TREE_A5 0x1p-1f
#define LUT_TREE_B5 (-0x1.2fcp-15f)
#include "lut-select-fp16-tree-body.h"

/*
**_Z29renamed_chained_coeff_cleanupv:
**	SFPLOADI	L0, 12640, 2
**	SFPLOADI	L0, 14298, 8.*
**	SFPLOADI	L1, 14576, 2
**	SFPLOADI	L1, 14561, 8.*
**	SFPLOADI	L2, 14422, 2
**	SFPLOADI	L2, 14336, 8.*
**	SFPLOADI	L3, 32959, 2
**	SFPLOADI	L3, 45313, 8.*
**	SFPMOV	L4, L3, 2
**	SFPLOADI	L3, 46192, 2
**	SFPLOADI	L3, 46136, 8.*
**	SFPMOV	L5, L3, 2
**	SFPLOADI	L3, 45056, 2
**	SFPLOADI	L3, 32959, 8.*
**	SFPMOV	L6, L3, 2
**	li	a5,8
**	SFPLOAD	L3, 0, 0, 7
**	SFPLUTFP32	L3, 2.*
**	SFPSTORE	L3, 0, 0, 7
**	TTINCRWC	0, 2, 0, 0
**	addi	a5,a5,-1
**	bne	a5,zero,.*
**	ret
*/
