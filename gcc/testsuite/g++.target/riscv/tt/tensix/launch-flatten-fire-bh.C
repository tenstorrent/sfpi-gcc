// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// Fire witness (the lane-HD topk phase-driver shape): the delivery
// census admits the counted loop, the complete unroller receives the
// proven trip count, the launch stream flattens to straight-line
// playbacks, and the per-trip conditionals (direction flip-flop, the
// record-once init guard) fold at their proven values -- the raw config
// word survives exactly on the odd trips, the record exactly once.
// { dg-final { scan-tree-dump "launch-flatten: requested complete unroll of loop \[0-9\]+ \\(~\[0-9\]+ delivery words/trip, trips 16\\)" "rvtt_launch_flatten" } }
// { dg-final { scan-assembler-times "TTREPLAY\t16, 9, 0, 0" 31 } }
// { dg-final { scan-assembler-times "TTREPLAY\t16, 9, 1, 1" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 8 } }

#include "launch-flatten-body.h"
