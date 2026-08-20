// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-loop-unroll -fdump-tree-rvtt_replay_unroll" }
// Fire witness: the typed census admits the counted SFPU row loop, the
// generic unroller receives the cost-table group factor, and the
// always-on replay former captures the unrolled row as one
// execute-while-record pass plus one-word playback launches per group.
// { dg-final { scan-tree-dump "requested unroll 8 of loop" "rvtt_replay_unroll" } }
// { dg-final { scan-assembler "TTREPLAY\t0, [0-9]+, 1, 1" } }
// { dg-final { scan-assembler "TTREPLAY\t0, [0-9]+, 0, 0" } }

#include "replay-loop-unroll-body.h"
