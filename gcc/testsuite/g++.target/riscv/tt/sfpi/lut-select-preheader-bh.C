// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -fdump-tree-rvtt_lut_select" }
// Coefficient placement without any sign restore: once the tree's CC
// scaffolding dissolves into the LUT, the counted loop's coefficient
// materializations are ordinary invariants and move to the loop
// preheader; the row body keeps no per-row immediate materialization.
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-update" 1 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-times "placed coefficient materialization in loop preheader" 6 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump "placements=6" "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }

#define LUT_TREE_FN lut_tree_preheader
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
