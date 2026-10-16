// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mno-tt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-completion-guard -fdump-rtl-rvtt_replay-details" }
// Check that the completion guard prices the re-record window with the delivery-bound model and refuses the hoist.
// { dg-final { scan-rtl-dump "Hoist pricing .loop \\d+.: trips 4, words 6, exec_ilk 6 slots .re-record body, delivery-bound., deliver_body 738, deliver_record 861, record 1161, before 861, after 670, benefit -397 .min 60." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -397 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-assembler-not "TTREPLAY\t0, 6, 0, 1" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 0" 1 } }

#include "record-hoist-body.h"
