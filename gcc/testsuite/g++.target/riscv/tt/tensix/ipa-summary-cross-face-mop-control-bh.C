// Independent positive control: the first MOP formation is supported while
// all its composing callees are live; the second conservatively sees an
// already-expanded sibling. Neither query may consume a stale GIMPLE body.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-mop-form -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump "ipa-summary: mop-face digest built \\(void kernel\\(int\\)/\\d+, \\d+ blocks\\)" "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "MOP formed \\(mop0-lA-replay, counted loop\\): 20 iterations of launch \\\[0,\\+3\\) -> TTMOP 0, 19, 0" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "mop-caller-template-live-unproven.: unanalyzable callee" 1 "rvtt_mop_form" } }

#define IPA_MOP_CONTROL 1
#include "ipa-summary-cross-face-chk-bh.C"
