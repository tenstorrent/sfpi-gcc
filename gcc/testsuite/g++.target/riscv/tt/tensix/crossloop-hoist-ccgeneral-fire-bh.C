// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_crossloop" }
// THE R2 CROSSLOOP FIRE (FABLE_GOES_BURR R2; the crossloop-cc-unproven
// widening): the tile loop carries a BALANCED structured CC frame
// (plain PUSHC, an audited narrowing SETCC refinement, plain POPC).
// Historically any CC writer in the crossed body stopped the placement
// walk by name; under the stage-B flag the CC-region tree proves the
// loop's CC activity ambient-preserving-and-narrowing -- the popc
// restores the saved enable state, so the enable set at the row
// placement stays a subset of the lifted entry's -- and both
// row-invariant materializations lift across the tile loop.
// { dg-final { scan-tree-dump "CC activity tree-proven ambient-preserving" "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-times "crossloop-hoist: hoisted across loop" 2 "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "refused" "rvtt_crossloop" } }

#define XLH_TILE_EXTRA() \
  do { \
    auto xg = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7); \
    __builtin_rvtt_sfppushc (0); \
    __builtin_rvtt_sfpsetcc_v (xg, 0); \
    __builtin_rvtt_sfppopc (0); \
  } while (0)
#define XLH_KERNEL xlh_ccgeneral_fire
#define XLH_TILES tiles
#define XLH_T t
#define XLH_ROW row
#define XLH_X x
#define XLH_C0 c0
#define XLH_C1 c1
#define XLH_VAL_C0 0x3e4b1a3d
#define XLH_VAL_C1 0x3f2e8ba3
#include "crossloop-hoist-body.h"
