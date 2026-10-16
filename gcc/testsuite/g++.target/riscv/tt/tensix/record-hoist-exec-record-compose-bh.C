// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-replay-exec-record -fdump-rtl-rvtt_replay-details" }
// Check that with replay-exec-record also enabled, a record-hoisted capture stays a no-exec record.
// { dg-final { scan-rtl-dump "record-hoist: invariant re-record window admitted" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Exec-while-record: capture" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 1" 1 } }
#include "record-hoist-body.h"
