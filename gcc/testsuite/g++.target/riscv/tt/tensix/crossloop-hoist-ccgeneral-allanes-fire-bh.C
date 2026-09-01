// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_crossloop" }
// R2 crossloop ALL-LANES-ENTRY FIRE (the canonical-form arm the
// post-rvtt_cc placement walks need): the crossed tile loop carries an
// arbitrary typed CC atom (here a lane-enable rewrite, the historical
// crossloop-cc-unproven witness), but the LIFTED ENTRY is proven
// ALL-LANES by the backward kill-modeling walk (function entry, no
// prior CC) -- a placement there writes EVERY lane, so any crossed
// enable state is a subset of the placement's and the containment
// fact holds unconditionally.  Both materializations lift.
// { dg-final { scan-tree-dump "proven ALL-LANES .cc-region-general.: crossed CC atoms admitted" "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-times "crossloop-hoist: hoisted across loop" 2 "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "refused" "rvtt_crossloop" } }

#define XLH_TILE_EXTRA() __builtin_rvtt_sfpencc (0, 10)
#define XLH_KERNEL xlh_ccgeneral_allanes
#define XLH_TILES tiles
#define XLH_T t
#define XLH_ROW row
#define XLH_X x
#define XLH_C0 c0
#define XLH_C1 c1
#define XLH_VAL_C0 0x3e4b1a3d
#define XLH_VAL_C1 0x3f2e8ba3
#include "crossloop-hoist-body.h"
