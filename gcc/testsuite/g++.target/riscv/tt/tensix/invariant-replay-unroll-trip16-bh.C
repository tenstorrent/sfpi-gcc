// Near miss for the gimple unroll REQUEST: sixteen proven iterations reach
// its structural bound, so no request is recorded and the invariant hoist
// still proceeds.  Under the interlock-aware reissue pricing the
// serially-chained residual is execution-bound, so the RTL counted-loop
// hoist also refuses (dump arithmetic asserted) and the scalar loop
// survives -- the corrected model's canonical exec-bound near miss.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -fdump-tree-rvtt_invariant-details -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-tree-dump-not "Requested complete unroll" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 6 "rvtt_invariant" } }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -2281 < 60" "rvtt_replay" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }
// { dg-final { scan-assembler "\\tbne\\t" } }

#define REPLAY_TRIP_COUNT 16
#include "invariant-replay-unroll-varied-body.h"
