// Check that "#pragma GCC unroll 1" suppresses the complete-unroll request but not the loadi hoist on Wormhole.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-tree-rvtt_invariant-details -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-tree-dump-times "Requested complete unroll for constant replay loop" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 12 "rvtt_invariant" } }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -251 < 0" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Unrolled launch loop" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 8 } }
// { dg-final { scan-assembler "\\tbne\\t" } }

#include "invariant-replay-unroll-pragma-body.h"
