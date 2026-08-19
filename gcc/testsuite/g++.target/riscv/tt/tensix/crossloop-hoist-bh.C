// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_crossloop" }
// The cross-loop hoist: the two row-invariant materializations (placed
// in the row preheader by the early invariant pass) lift across the
// tile loop, whose body delivers only audited-inert words (SETRWC,
// sync family, composed thread-config push).
// { dg-final { scan-tree-dump-times "crossloop-hoist: hoisted across loop" 2 "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "refused" "rvtt_crossloop" } }

#define XLH_KERNEL xlh_fire_kernel
#define XLH_TILES tiles
#define XLH_T t
#define XLH_ROW row
#define XLH_X x
#define XLH_C0 c0
#define XLH_C1 c1
#define XLH_VAL_C0 0x3e4b1a3d
#define XLH_VAL_C1 0x3f2e8ba3
#include "crossloop-hoist-body.h"
