// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_crossloop" }
// A CC-writing statement (a bare lane-enable rewrite) in the tile
// loop: the hoisted materialization would execute under a different
// lane-enable state; refuse by name.
// { dg-final { scan-tree-dump "refused .crossloop-cc-unproven." "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "hoisted across" "rvtt_crossloop" } }

#define XLH_TILE_EXTRA() __builtin_rvtt_sfpencc (0, 10)
#define XLH_KERNEL xlh_cc_kernel
#define XLH_TILES tiles
#define XLH_T t
#define XLH_ROW row
#define XLH_X x
#define XLH_C0 c0
#define XLH_C1 c1
#define XLH_VAL_C0 0x3e4b1a3d
#define XLH_VAL_C1 0x3f2e8ba3
#include "crossloop-hoist-body.h"
