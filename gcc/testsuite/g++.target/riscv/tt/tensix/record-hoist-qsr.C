// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// Check that record-hoist refuses on QSR, which has no audited result latencies, even with a zero minimum benefit.
// { dg-final { scan-rtl-dump "record-hoist refused: replay-reissue-latency-unproved" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
#include "record-hoist-body.h"
