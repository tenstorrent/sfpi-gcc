// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mno-tt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-completion-guard -fdump-rtl-rvtt_replay-details" }
// WH twin: exact shared-model arithmetic and legal-domain ordering.
// { dg-final { scan-rtl-dump "Replay completion guard: execution-bound re-record charges hoisted delivery 615 .record cost 915." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoist pricing .loop \\d+.: trips 4, words 4, exec_ilk 8 slots .re-record body, execution-bound., deliver_body 492, deliver_record 615, record 915, before 1100, after 870, benefit 5 .min 60." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit 5 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 4, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 4, 0, 0" 1 } }

#include "record-hoist-completion-execbound-body.h"
