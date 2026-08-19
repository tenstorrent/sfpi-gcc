// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant" }
// Default-off: without -mtt-tensix-optimize-crossloop-hoist the early
// invariant pass still refuses the tile loop wholesale (opaque LREG
// state) and the materializations stay in the row preheader inside it.
// { dg-final { scan-tree-dump "function has opaque LREG state" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }

#define XLH_KERNEL xlh_off_kernel
#define XLH_TILES tiles
#define XLH_T t
#define XLH_ROW row
#define XLH_X x
#define XLH_C0 c0
#define XLH_C1 c1
#define XLH_VAL_C0 0x3e4b1a3d
#define XLH_VAL_C1 0x3f2e8ba3
#include "crossloop-hoist-body.h"
