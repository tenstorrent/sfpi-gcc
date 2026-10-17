// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-min-benefit=2000 -fdump-rtl-rvtt_replay-details" }
// Check that the record hoist refuses by name when the modeled benefit is below the threshold.
// { dg-final { scan-rtl-dump "Not hoisting: record-hoist-benefit: modeled issue-side benefit 1511 < 2000" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
#include "record-hoist-body.h"
