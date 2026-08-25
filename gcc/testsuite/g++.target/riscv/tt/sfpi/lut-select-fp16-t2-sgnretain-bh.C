// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-fp16 -fdump-tree-rvtt_lut_select" }
// The TABLE2 boundary set (top boundary 4.0) with a trailing setsgn
// consumer selects the sign-retain TABLE2 mode (mod0 0x7) and the
// explicit sign copy dissolves into the mode word.
// { dg-final { scan-tree-dump-times "formed fp16-6entry-t2-sgn-retain \\(mod0 0x7\\) from 6-range magnitude dispatch tree, boundaries 0x3f000000,0x3f800000,0x3fc00000,0x40000000,0x40800000, slot leaves affine,affine,affine,affine,affine,affine" 1 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-times "folded trailing sign restore" 1 "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// { dg-final { scan-assembler-not "SFPSETSGN" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }

#define LUT_TREE_FN lut_tree_fp16_t2_sgn
#define LUT_TREE_X x
#define LUT_TREE_MAG mag
#define LUT_TREE_R r
#define LUT_TREE_TOP 4.0f
#define LUT_TREE_SETSGN 1
#define LUT_TREE_A0 0.24609375f
#define LUT_TREE_B0 (-0.00048828125f)
#define LUT_TREE_A1 0.216796875f
#define LUT_TREE_B1 0.0152587890625f
#define LUT_TREE_A2 0.173828125f
#define LUT_TREE_B2 0.059814453125f
#define LUT_TREE_A3 0.42578125f
#define LUT_TREE_B3 (-0.318359375f)
#define LUT_TREE_A4 0.048583984375f
#define LUT_TREE_B4 0.30078125f
#define LUT_TREE_A5 0.0f
#define LUT_TREE_B5 0.499755859375f
#include "lut-select-fp16-tree-body.h"
