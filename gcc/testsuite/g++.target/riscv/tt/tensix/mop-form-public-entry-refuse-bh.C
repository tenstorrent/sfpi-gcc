// A public definition with no in-TU caller is not thereby a crt0-benign
// kernel entry.  An unseen external caller can invoke it and launch the
// thread-shared MOP template after it returns.  Formation must fail closed
// instead of treating the empty caller list as proof of outward ownership.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-mop-form -mtt-tensix-mop-form-force -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump "MOP-form refused \\(mop-caller-template-live-unproven\\): externally callable function on the caller chain" "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-not "MOP formed" "rvtt_mop_form" } }
// { dg-final { scan-assembler-not "TTMOP\\t" } }

#include "replay-counted-body.h"
