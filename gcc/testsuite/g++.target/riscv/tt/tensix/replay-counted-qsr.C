// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// Check that on QSR, which has no result-latency facts, the counted-loop replay hoist refuses (replay-reissue-latency-unproved) even with min-benefit=0.
// { dg-final { scan-rtl-dump "replay-reissue-latency-unproved" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-assembler "\\tbne\\t" } }

#include "replay-counted-body.h"
