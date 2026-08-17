// { dg-options "-mcpu=tt-qsr32-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -fdump-tree-rvtt_lut_select" }
// Quasar has no validated LUT capability: hard refusal, no formation.
// { dg-final { scan-tree-dump "lut-select: refused \\(lut-no-target-capability\\)" "rvtt_lut_select" } }
// { dg-final { scan-assembler-not "SFPLUTFP32" } }

#define LUT_TREE_FN lut_tree_qsr
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
