// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-fp16 -ffinite-math-only -fdump-tree-rvtt_lut_select" }
// Wormhole under the function's own -ffinite-math-only license: the
// negative-NaN bucket divergence is excluded by the license, so the
// FP16 six-entry formation is admitted.
// { dg-final { scan-tree-dump-times "formed fp16-6entry-t1-sgn-update \\(mod0 0x2\\) from 6-range magnitude dispatch tree" 1 "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }

#define LUT_TREE_FN lut_tree_fp16_wh_finite
#define LUT_TREE_X x
#define LUT_TREE_MAG mag
#define LUT_TREE_R r
#define LUT_TREE_TOP 3.0f
#define LUT_TREE_A0 0.1875f
#define LUT_TREE_B0 0.3125f
#define LUT_TREE_A1 0.265625f
#define LUT_TREE_B1 (-0.044921875f)
#define LUT_TREE_A2 0.59375f
#define LUT_TREE_B2 (-0.28125f)
#define LUT_TREE_A3 0.609375f
#define LUT_TREE_B3 (-0.263671875f)
#define LUT_TREE_A4 0.5390625f
#define LUT_TREE_B4 (-0.119140625f)
#define LUT_TREE_A5 0.5f
#define LUT_TREE_B5 0.0f
#include "lut-select-fp16-tree-body.h"
