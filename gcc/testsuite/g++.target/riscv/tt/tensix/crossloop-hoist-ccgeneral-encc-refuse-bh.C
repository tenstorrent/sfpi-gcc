// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_crossloop" }
// R2 crossloop near miss: an SFPENCC in the crossed tile loop can
// WIDEN the lane-enable state beyond the lifted entry's ambient (the
// entry is not proven all-lanes here), so the tree cannot prove the
// loop ambient-preserving-and-narrowing -- the walk stops by the
// widening's OWN name and the materializations stay in the row
// preheader.
// { dg-final { scan-tree-dump "crossloop-cc-ambient-unproven" "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "hoisted across" "rvtt_crossloop" } }

#define XLH_TILE_EXTRA() __builtin_rvtt_sfpencc (0, 10)
#define XLH_KERNEL xlh_ccgeneral_encc
#define XLH_TILES tiles
#define XLH_T t
#define XLH_ROW row
#define XLH_X x
#define XLH_C0 c0
#define XLH_C1 c1
#define XLH_VAL_C0 0x3e4b1a3d
#define XLH_VAL_C1 0x3f2e8ba3
#include "crossloop-hoist-body.h"
