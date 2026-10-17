// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-capture-rotation -fdump-rtl-rvtt_replay-details" }
// Check that record-hoist fires unchanged when capture rotation is also enabled.
// { dg-final { scan-rtl-dump "record-hoist: invariant re-record window admitted \\(trips 4, words 6, benefit 1511\\)" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
#include "record-hoist-body.h"
