// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// Wormhole fire witness: the same structural admission and flatten.
// { dg-final { scan-tree-dump "launch-flatten: requested complete unroll of loop \[0-9\]+ \\(~\[0-9\]+ delivery words/trip, trips 16\\)" "rvtt_launch_flatten" } }
// { dg-final { scan-assembler-times "TTREPLAY\t16, 9, 0, 0" 31 } }
// { dg-final { scan-assembler-times "TTREPLAY\t16, 9, 1, 1" 1 } }

#include "launch-flatten-body.h"
