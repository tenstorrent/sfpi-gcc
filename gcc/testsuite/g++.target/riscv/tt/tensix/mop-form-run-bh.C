// MOP formation over the run of launches the replay hoist + launch-loop
// unroll leave behind: the 8-trip counted payload becomes a hoisted
// no-exec capture plus 8 contiguous playback launches, and the mop-form
// pass re-rolls those into one TTMOP with the capture intact.  The run's
// rows are execution-bound (len 4), so the corrected delivery model
// prices the formation negative and only the testing/measurement force
// flag builds this leg (the hardware A/B shape).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-mop-form -mtt-tensix-mop-form-force -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump-times "MOP-form candidate \\(run\\): 32 x launch \\\[0,\\+8\\), config 9 words, modeled benefit -1107" 1 "rvtt_mop_form" } }
// { dg-final { scan-rtl-dump-times "MOP formed \\(mop0-lA-replay, run\\): 32 iterations of launch \\\[0,\\+8\\) -> TTMOP 0, 31, 0" 1 "rvtt_mop_form" } }
// { dg-final { scan-assembler-times "TTMOP\\t0, 31, 0" 1 } }
// { dg-final { scan-assembler-times "TTMOPCFG\\t0" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY" 1 } }
// { dg-final { scan-assembler-not "\\tbne\\t" } }

#include "replay-counted-body.h"
