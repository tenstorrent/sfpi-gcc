// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mno-tt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-completion-guard -fdump-rtl-rvtt_replay-details" }
// WH twin of the guarded runtime-trip refusal.  The zero-trip arithmetic is
// diagnostic only; the completion shared model requires a proven count.
// { dg-final { scan-rtl-dump "Hoist pricing .loop \\d+.: trips 0, words 6, exec_ilk 6 slots .re-record body, delivery-bound., deliver_body 738, deliver_record 861, record 1161, before 861, after 670, benefit -1161 .min 60." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Not hoisting: record-hoist-completion-runtime-trips-unproven: completion-accurate shared model requires a proven trip count .loop \\d+." "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Not hoisting: modeled benefit -1161 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-assembler-not "TTREPLAY\t0, 6, 0, 1" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 0" 1 } }

#include "record-hoist-completion-runtime-body.h"
