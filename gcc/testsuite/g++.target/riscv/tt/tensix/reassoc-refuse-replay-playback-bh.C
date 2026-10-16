// Check that a TTREPLAY playback inside the chain window refuses the rebalance (reassoc-replay-playback-boundary).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_reassoc" }
// { dg-final { scan-tree-dump-times "reassoc-replay-playback-boundary" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "integer rebalance" "rvtt_reassoc" } }
#define RA_KERNEL ra_xor_replay_window
#define RA_N 4
#define RA_OP(a, b) __builtin_rvtt_sfpxor ((a), (b))
#define RA_MID() __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 1)
#define RA_X0 x0
#define RA_X1 x1
#define RA_X2 x2
#define RA_X3 x3
#define RA_S1 s1
#define RA_S2 s2
#define RA_SL s3
#include "reassoc-body.h"
