// The same hoisted-and-unrolled launch run as mop-form-run-bh.C, priced
// under the corrected concurrent-delivery model with the threshold
// override at its floor: an execution-bound row (len 4, 400 >= 123
// centislots) gains nothing from removing its launch push, so the
// modeled benefit is the pure configuration cost (-1107) and even
// -mtt-tensix-mop-form-min-benefit=0 refuses byte-identically -- the
// launches and the capture survive unchanged.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-mop-form -mtt-tensix-mop-form-min-benefit=0 -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump-times "MOP-form refused \\(mop-profitability\\): modeled benefit -1107 below threshold" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-not "MOP formed" "rvtt_mop_form" } }
// { dg-final { scan-assembler-not "TTMOP" } }
// { dg-final { scan-assembler-times "TTREPLAY" 9 } }

#include "replay-counted-body.h"
