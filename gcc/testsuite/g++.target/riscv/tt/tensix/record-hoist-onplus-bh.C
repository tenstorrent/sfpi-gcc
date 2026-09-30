// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay-details" }
// Composition twin: with the plain hoist AND the record-hoist enabled
// (the on-plus measurement leg shape) the counted-loop machinery runs
// first, does not claim this non-payload body, and the record-hoist
// admission fires on the sequence path exactly as in the solo leg.
// { dg-final { scan-rtl-dump "record-hoist: invariant re-record window admitted \\(trips 4, words 6, benefit 1511\\)" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 1" 1 } }
#include "record-hoist-body.h"
