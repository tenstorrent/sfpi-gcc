// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -fdump-tree-rvtt_lut_select" }
// Value-independence twin: every name and every coefficient differs
// from lut-select-bh.C; only the dataflow shape and the architectural
// boundaries (1.0f, 2.0f) are shared.  The decision must be identical.
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-update" 1 "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPMAD" } }

#define LUT_TREE_FN totally_different_op
#define LUT_TREE_X input_row
#define LUT_TREE_MAG magnitude
#define LUT_TREE_R answer
#define LUT_TREE_A0 (-0.7321f)
#define LUT_TREE_B0 1.1250f
#define LUT_TREE_A1 0.0069f
#define LUT_TREE_B1 42.5f
#define LUT_TREE_A2 (-3.1416f)
#define LUT_TREE_B2 (-0.0001f)
#include "lut-select-tree-body.h"
