// Near miss: sixteen proven iterations reach the structural bound, so the
// unroll request must be refused while the invariant hoist still proceeds
// and the scalar loop control survives.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-not "Requested complete unroll" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 6 "rvtt_invariant" } }
// { dg-final { scan-assembler "\\tbne\\t" } }

#define REPLAY_TRIP_COUNT 16
#include "invariant-replay-unroll-varied-body.h"
