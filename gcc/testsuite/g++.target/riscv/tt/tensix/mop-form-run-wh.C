// Check that the playback launches of a hoisted capture form a MOP run candidate that the cost model refuses (mop-profitability).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-mop-form -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump-times "MOP-form refused \\(mop-profitability\\): modeled benefit -\\d+ below threshold" 1 "rvtt_mop_form" } }
// { dg-final { scan-assembler-not "TTMOP" } }

#include "replay-counted-body.h"
