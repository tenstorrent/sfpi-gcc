// Lane FL (FH-3, licensed direction): the FULL FP license
// (-fassociative-math -fno-signed-zeros -fno-trapping-math +
// -mtt-tensix-optimize-reassoc) never overrides the playback barrier --
// a licensed FP chain with a TTREPLAY playback inside its window still
// refuses BY NAME.  The license covers rounding, not delivery order.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_reassoc" }
// { dg-final { scan-tree-dump-times "reassoc-replay-playback-boundary" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "licensed rebalance" "rvtt_reassoc" } }
#define RA_KERNEL ra_add_replay_window
#define RA_N 4
#define RA_MID() __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 1)
#define RA_X0 y0
#define RA_X1 y1
#define RA_X2 y2
#define RA_X3 y3
#define RA_S1 t1
#define RA_S2 t2
#define RA_SL t3
#include "reassoc-body.h"
