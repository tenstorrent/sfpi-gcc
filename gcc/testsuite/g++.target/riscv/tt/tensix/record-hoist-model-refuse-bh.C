// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// Check that the plain replay-hoist model refuses this re-record window and no record-hoist output appears.
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -397 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "record-hoist:" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "record-hoist refused" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Record-hoist pricing" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
#include "record-hoist-body.h"
