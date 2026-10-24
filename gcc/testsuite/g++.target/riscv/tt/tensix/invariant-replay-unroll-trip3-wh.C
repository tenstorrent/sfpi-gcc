// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Requested complete unroll for constant replay loop" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 6 "rvtt_invariant" } }
// { dg-final { scan-assembler-times "TTREPLAY" 3 } }
// { dg-final { scan-assembler-not "\\tbne\\t" } }

#include "invariant-replay-unroll-varied-body.h"
