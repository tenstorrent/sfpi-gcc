// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-min-benefit=2000 -fdump-rtl-rvtt_replay-details" }
// Named refusal: the issue-side benefit sits below the (overridden)
// threshold; the record-hoist refuses by name and the in-body record
// stays byte-identical.
// { dg-final { scan-rtl-dump "Not hoisting: record-hoist-benefit: modeled issue-side benefit 1511 < 2000" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
#include "record-hoist-body.h"
