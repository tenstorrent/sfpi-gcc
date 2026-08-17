// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -fdump-tree-rvtt_lut_select" }
// Value-independence twin of lut-select-sgnretain-bh.C: every name and
// every coefficient differs; only the dataflow shape and the
// architectural boundaries (1.0f, 2.0f) are shared.  The fold and the
// coefficient placement decisions must be identical.
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-retain \\(mod0 0x4\\)" 1 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-times "folded trailing sign restore into the LUT mode word" 1 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-times "placed coefficient materialization in loop preheader" 6 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump "placements=6" "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32\tL\[0-7\], 4" 1 } }
// { dg-final { scan-assembler-not "SFPSETSGN" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }

#define LUT_TREE_FN completely_other_activation
#define LUT_TREE_X row_value
#define LUT_TREE_MAG size_part
#define LUT_TREE_R chosen
#define LUT_TREE_A0 (-1.6180f)
#define LUT_TREE_B0 0.7071f
#define LUT_TREE_A1 2.7182f
#define LUT_TREE_B1 (-0.5772f)
#define LUT_TREE_A2 0.0061f
#define LUT_TREE_B2 13.371f
#define LUT_TREE_EPI (-0.3679f)
#include "lut-select-sgn-tree-body.h"
