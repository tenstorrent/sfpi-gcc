// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -fdump-tree-rvtt_lut_select" }
// Wormhole has the same FP32 3-entry sign-retain capability; the fold
// and the coefficient placement fire identically.
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-retain \\(mod0 0x4\\)" 1 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-times "folded trailing sign restore into the LUT mode word" 1 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump "placements=6" "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32\tL\[0-7\], 4" 1 } }
// { dg-final { scan-assembler-not "SFPSETSGN" } }

#define LUT_TREE_FN lut_tree_sgn_wh
#define LUT_TREE_X x
#define LUT_TREE_MAG mag
#define LUT_TREE_R r
#define LUT_TREE_A0 0.2452f
#define LUT_TREE_B0 (-0.0005f)
#define LUT_TREE_A1 0.1497f
#define LUT_TREE_B1 0.0814f
#define LUT_TREE_A2 0.0375f
#define LUT_TREE_B2 0.3058f
#define LUT_TREE_EPI 0.5f
#include "lut-select-sgn-tree-body.h"
