// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-fp16 -fdump-tree-rvtt_lut_select" }
// Renaming every SSA-visible identifier and changing every coefficient
// to a DIFFERENT LUT16-exact value must not change the selection
// decision: the matcher keys only on the dataflow shape, the
// architectural boundaries, and the encodability proofs.
// { dg-final { scan-tree-dump-times "formed fp16-6entry-t1-sgn-update \\(mod0 0x2\\) from 6-range magnitude dispatch tree, boundaries 0x3f000000,0x3f800000,0x3fc00000,0x40000000,0x40400000, slot leaves affine,affine,affine,affine,affine,affine" 1 "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }

#define LUT_TREE_FN completely_different_name
#define LUT_TREE_X input_value
#define LUT_TREE_MAG magnitude
#define LUT_TREE_R selected
#define LUT_TREE_TOP 3.0f
#define LUT_TREE_A0 0.9375f
#define LUT_TREE_B0 (-0.015625f)
#define LUT_TREE_A1 0.75f
#define LUT_TREE_B1 0.0859375f
#define LUT_TREE_A2 0.4375f
#define LUT_TREE_B2 0.404296875f
#define LUT_TREE_A3 0.212890625f
#define LUT_TREE_B3 0.7421875f
#define LUT_TREE_A4 0.060546875f
#define LUT_TREE_B4 1.046875f
#define LUT_TREE_A5 0.0f
#define LUT_TREE_B5 1.21875f
#include "lut-select-fp16-tree-body.h"
