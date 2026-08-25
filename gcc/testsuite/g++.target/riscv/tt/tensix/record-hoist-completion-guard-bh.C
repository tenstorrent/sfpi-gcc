// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mno-tt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-completion-guard -fdump-rtl-rvtt_replay-details" }
// Record-hoist-only uses its dedicated delivery model.  That model already
// charges the complete seven-word preheader record (capture plus six payload
// words), so the completion guard witnesses the contract without double
// charging it or changing the established 1511-centislot admission.
// { dg-final { scan-rtl-dump "Replay completion guard: record-hoist pricing already charges complete hoisted delivery 861 .record cost 1161." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Record-hoist pricing .loop \\d+.: trips 4, words 6, deliver_body 738/trip, boundary 70/trip, record_once 1161, benefit 1511 .min 60." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist: invariant re-record window admitted \\(trips 4, words 6, benefit 1511\\)" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Replay completion guard: execution-bound re-record" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 0" 2 } }

#include "record-hoist-body.h"
