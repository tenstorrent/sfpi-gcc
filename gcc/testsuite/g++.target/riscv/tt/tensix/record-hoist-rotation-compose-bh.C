// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-capture-rotation -fdump-rtl-rvtt_replay-details" }
// Composition with capture-rotation (DL): rotation runs in the earlier
// hazard-scheduler pass and admits only capturable-row counted loops
// (single fixed run bodies); this re-record shape is not in its class,
// its stream reaches replay formation unchanged, and the record-hoist
// fires exactly as in the solo leg.
// { dg-final { scan-rtl-dump "record-hoist: invariant re-record window admitted \\(trips 4, words 6, benefit 1511\\)" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
#include "record-hoist-body.h"
