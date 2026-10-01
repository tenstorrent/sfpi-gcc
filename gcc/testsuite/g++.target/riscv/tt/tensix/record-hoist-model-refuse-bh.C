// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// Class-boundary twin: the plain replay-hoist's calibrated re-record
// model refuses this shape (delivery-bound, run 1: benefit
// 4*(861-670) - 1161 = -397); only the record-hoist measurement flag
// admits it.  Keeps the Log-class hardware-anchored default intact.
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -397 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "record-hoist:" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "record-hoist refused" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Record-hoist pricing" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
#include "record-hoist-body.h"
