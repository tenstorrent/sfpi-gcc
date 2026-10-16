// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -fdump-rtl-rvtt_replay-details" }
// Check that without -mtt-tensix-optimize-replay-record-hoist the re-record loop keeps its in-body exec-record.
// { dg-final { scan-rtl-dump-not "record-hoist:" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "record-hoist refused" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Record-hoist pricing" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 1, 1" 1 } }
#include "record-hoist-body.h"
