// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-loop-unroll -fdump-tree-rvtt_replay_unroll" }
// Check that the counted SFPU row loop is unrolled by the group factor and captured as one record plus playbacks.
// { dg-final { scan-tree-dump "requested unroll 8 of loop" "rvtt_replay_unroll" } }
// { dg-final { scan-assembler "TTREPLAY\t0, \[0-9\]+, 1, 1" } }
// { dg-final { scan-assembler "TTREPLAY\t0, \[0-9\]+, 0, 0" } }

#include "replay-loop-unroll-body.h"
