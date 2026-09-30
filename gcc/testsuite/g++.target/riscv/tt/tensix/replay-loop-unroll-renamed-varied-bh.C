// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-loop-unroll -fdump-tree-rvtt_replay_unroll" }
// Renamed function, varied trip count and immediate: the decision is
// structural, so the request still fires (trips 24 also group by 8).
// { dg-final { scan-tree-dump "requested unroll 8 of loop" "rvtt_replay_unroll" } }
// { dg-final { scan-assembler "TTREPLAY\t0, \[0-9\]+, 1, 1" } }

#define RLU_KERNEL some_other_row_engine
#define RLU_TRIPS 24
#define RLU_C0 0x3e2a
#include "replay-loop-unroll-body.h"
