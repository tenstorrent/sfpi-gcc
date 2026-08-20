// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-loop-unroll -fdump-tree-rvtt_replay_unroll" }
// Fire witness on Wormhole: same typed admission, same request.
// { dg-final { scan-tree-dump "requested unroll 8 of loop" "rvtt_replay_unroll" } }
// { dg-final { scan-assembler "TTREPLAY\t0, [0-9]+, 1, 1" } }

#define RLU_MODE 0
#include "replay-loop-unroll-body.h"
