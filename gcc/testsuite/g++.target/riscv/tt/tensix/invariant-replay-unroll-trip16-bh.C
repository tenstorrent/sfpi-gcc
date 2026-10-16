// Check that a sixteen-trip loop gets no unroll request or replay hoist but still hoists loadi.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -fdump-tree-rvtt_invariant-details -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-tree-dump-not "Requested complete unroll" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 6 "rvtt_invariant" } }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -2281 < 60" "rvtt_replay" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }
// { dg-final { scan-assembler "\\tbne\\t" } }

#define REPLAY_TRIP_COUNT 16
#include "invariant-replay-unroll-varied-body.h"
