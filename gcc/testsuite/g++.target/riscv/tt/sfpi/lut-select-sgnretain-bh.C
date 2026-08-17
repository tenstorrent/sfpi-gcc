// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -fdump-tree-rvtt_lut_select" }
// A 3-range |x| dispatch tree whose selected value's single consumer
// restores the input's sign forms the sign-retain LUT mode: the copy
// folds into the mode word (mod0 bit 2) and the explicit sign-copy
// instruction dissolves.  The now-CC-free counted loop then places all
// six coefficient materializations in the loop preheader; the row body
// keeps only load / LUT / epilogue / store.
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-retain \\(mod0 0x4\\) from 3-range magnitude dispatch tree, boundaries 0x3f800000,0x40000000" 1 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-times "folded trailing sign restore into the LUT mode word" 1 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-times "placed coefficient materialization in loop preheader" 6 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump "placements=6" "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32\tL\[0-7\], 4" 1 } }
// { dg-final { scan-assembler-not "SFPSETSGN" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPMAD" } }

#define LUT_TREE_FN lut_tree_sgn
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
