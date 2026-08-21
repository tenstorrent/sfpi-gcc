// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// QSR carries no audited result-latency facts (rvtt-cost.md), so the
// record-hoist's audit gate cannot prove the window's reissue: it
// refuses by name, and the min-benefit override cannot force it.
// { dg-final { scan-rtl-dump "record-hoist refused: replay-reissue-latency-unproved" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
#include "record-hoist-body.h"
