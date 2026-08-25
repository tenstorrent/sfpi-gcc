// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mno-tt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay-details" }
// WH twin: identical shared costs and audited next-slot acceptance stall.
// { dg-final { scan-rtl-dump "Record-hoist pricing .loop \\d+.: trips 4, words 4, deliver_body 492/trip, boundary 70/trip, record_once 915, benefit 773 .min 60." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist: invariant re-record window admitted .trips 4, words 4, benefit 773." "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Replay completion guard:" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 4, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 4, 0, 0" 2 } }

#include "record-hoist-completion-execbound-body.h"
