// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_crossloop" }
// The row loop (and so the row-preheader materializations) executes
// under a runtime condition inside the tile loop: hoisting would
// speculate the architectural LREG writes on iterations where the
// original never ran; refuse by name.
// { dg-final { scan-tree-dump "refused .crossloop-speculation-unproven." "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "hoisted across" "rvtt_crossloop" } }

extern volatile int xlh_spec_gate;
#define XLH_GUARD_BEGIN if (xlh_spec_gate) {
#define XLH_GUARD_END }
#define XLH_KERNEL xlh_spec_kernel
#define XLH_TILES tiles
#define XLH_T t
#define XLH_ROW row
#define XLH_X x
#define XLH_C0 c0
#define XLH_C1 c1
#define XLH_VAL_C0 0x3e4b1a3d
#define XLH_VAL_C1 0x3f2e8ba3
#include "crossloop-hoist-body.h"
