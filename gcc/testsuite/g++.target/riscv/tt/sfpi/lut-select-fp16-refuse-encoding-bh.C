// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-fp16 -fdump-tree-rvtt_lut_select" }
// A coefficient whose FP32 value does not re-encode exactly in the
// LUT16 format (0.1f: 13 low mantissa bits nonzero) refuses by name
// and edits nothing.
// { dg-final { scan-tree-dump-times "refused \\(lut-coeff-encoding-unrepresentable\\)" 1 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "formed" "rvtt_lut_select" } }
// { dg-final { scan-assembler-not "SFPLUTFP32" } }
// { dg-final { scan-assembler "SFPSETCC" } }

#define LUT_TREE_FN lut_tree_fp16_bad_coeff
#define LUT_TREE_X x
#define LUT_TREE_MAG mag
#define LUT_TREE_R r
#define LUT_TREE_TOP 3.0f
#define LUT_TREE_A0 0.1875f
#define LUT_TREE_B0 0.3125f
#define LUT_TREE_A1 0.1f
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
