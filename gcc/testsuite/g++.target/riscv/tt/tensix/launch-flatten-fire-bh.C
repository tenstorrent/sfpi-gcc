// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// Check that a counted delivery loop flattens to straight-line playbacks with its per-trip conditionals folded.
// { dg-final { scan-tree-dump "launch-flatten: requested complete unroll of loop \[0-9\]+ \\(~\[0-9\]+ delivery words/trip, trips 16\\)" "rvtt_launch_flatten" } }
// { dg-final { scan-assembler-times "TTREPLAY\t16, 9, 0, 0" 31 } }
// { dg-final { scan-assembler-times "TTREPLAY\t16, 9, 1, 1" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 8 } }

#include "launch-flatten-body.h"
