// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mno-tt-tensix-optimize-lut-select" }
// OFF path: with -mno-tt-tensix-optimize-lut-select the tree keeps
// its predicated form.
// Default-ON promotion (silicon-validated: sigmoid-tree +194%->+8.9%):
// this test used to rely on the ambient default being off; it now pins
// the -mno- spelling so the disabled path stays covered.
// { dg-final { scan-assembler-not "SFPLUTFP32" } }
// { dg-final { scan-assembler "SFPSETCC" } }

#define LUT_TREE_FN lut_tree_off
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
