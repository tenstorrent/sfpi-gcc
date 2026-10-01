// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -fdump-tree-rvtt_lut_select" }
// A 3-range |x| dispatch tree with constant affine leaves forms one
// SFPLUTFP32 (FP32 3-entry, SGN_UPDATE); the predication scaffolding,
// leaf MADs, and the now-internal abs all disappear.
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-update \\(mod0 0\\) from 3-range magnitude dispatch tree, boundaries 0x3f800000,0x40000000" 1 "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPCOMPC" } }
// { dg-final { scan-assembler-not "SFPMAD" } }
// { dg-final { scan-assembler-not "SFPABS" } }

#define LUT_TREE_FN lut_tree
#define LUT_TREE_X x
#define LUT_TREE_MAG mag
#define LUT_TREE_R r
#define LUT_TREE_A0 0.1875f
#define LUT_TREE_B0 0.3125f
#define LUT_TREE_A1 0.2651f
#define LUT_TREE_B1 (-0.0442f)
#define LUT_TREE_A2 0.0913f
#define LUT_TREE_B2 0.4477f
#include "lut-select-tree-body.h"
