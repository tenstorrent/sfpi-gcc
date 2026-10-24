// Check that an execution-bound launch run refuses MOP formation even with -mtt-tensix-mop-form-min-benefit=0.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-mop-form -mtt-tensix-mop-form-min-benefit=0 -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump-times "MOP-form refused \\(mop-profitability\\): modeled benefit -1107 below threshold" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-not "MOP formed" "rvtt_mop_form" } }
// { dg-final { scan-assembler-not "TTMOP" } }
// { dg-final { scan-assembler-times "TTREPLAY" 33 } }

#include "replay-counted-body.h"
