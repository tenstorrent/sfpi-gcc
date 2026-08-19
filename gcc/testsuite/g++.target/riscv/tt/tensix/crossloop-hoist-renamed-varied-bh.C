// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_crossloop" }
// Renamed, constant-varied twin of the fire test: nothing may key on
// names or coefficient values.
// { dg-final { scan-tree-dump-times "crossloop-hoist: hoisted across loop" 2 "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "refused" "rvtt_crossloop" } }

#define XLH_KERNEL blend_rows_over_frames
#define XLH_TILES frames
#define XLH_T frame
#define XLH_ROW lane_row
#define XLH_X acc
#define XLH_C0 gain
#define XLH_C1 bias
#define XLH_VAL_C0 0xbd6b2f05
#define XLH_VAL_C1 0x3a91e677
#include "crossloop-hoist-body.h"
