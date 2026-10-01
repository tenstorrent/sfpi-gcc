// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// QSR carries no audited result-latency facts (rvtt-cost.md), so the
// corrected reissue model cannot price the payload: the hoist refuses by
// name and the loop keeps its scalar backedge.  The min-benefit override
// cannot force an unpriceable payload.
// { dg-final { scan-rtl-dump "replay-reissue-latency-unproved" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-assembler "\\tbne\\t" } }

#include "replay-counted-body.h"
