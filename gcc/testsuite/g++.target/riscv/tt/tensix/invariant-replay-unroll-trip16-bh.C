// Near miss for the gimple unroll REQUEST: sixteen proven iterations reach
// its structural bound, so no request is recorded and the invariant hoist
// still proceeds.  The scalar backedge is nevertheless removed later: the
// RTL counted-loop hoist leaves a pure launch loop and the launch-loop
// unroll (no size problem at 16 one-word trips) replicates it back to back.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-not "Requested complete unroll" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 6 "rvtt_invariant" } }
// { dg-final { scan-assembler-times "TTREPLAY" 17 } }
// { dg-final { scan-assembler-not "\\tbne\\t" } }

#define REPLAY_TRIP_COUNT 16
#include "invariant-replay-unroll-varied-body.h"
