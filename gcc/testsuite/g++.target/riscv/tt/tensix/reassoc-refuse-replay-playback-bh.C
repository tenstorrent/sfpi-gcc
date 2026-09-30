// A TTREPLAY playback point inside the chain window is
// a delivery BOUNDARY, not a transparent statement -- its recorded
// slots can contain CC or configuration writers (crosscall refuses the
// same class by name), so value-order across it is unproven and the
// chain refuses BY NAME (reassoc-replay-playback-boundary) instead of
// rebalancing links across the playback.  Applies to the proven
// integer class too: the hazard is delivery order, not rounding.
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
