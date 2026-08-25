// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -fdump-tree-rvtt_lut_select" }
// Without -mtt-tensix-optimize-lut-select-fp16 a 6-range tree keeps the
// historical two-region scan shape: the third range's else-branch
// refuses the partition arity and the tree's bytes are untouched.
// (The overlapping re-scans from the tree's inner pushc statements
// refuse by the same name, so the count is not pinned.)
// { dg-final { scan-tree-dump "refused \\(lut-partition-arity-unsupported\\)" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "formed" "rvtt_lut_select" } }
// { dg-final { scan-assembler-not "SFPLUTFP32" } }
// { dg-final { scan-assembler "SFPSETCC" } }

#define LUT_TREE_FN lut_tree_fp16_off
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
