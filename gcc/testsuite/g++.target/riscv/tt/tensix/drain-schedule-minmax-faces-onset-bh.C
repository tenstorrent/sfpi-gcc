// Check that the elided drains survive the full optimization flag set (3 SFPNOPs).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-latency-schedule -mtt-tensix-optimize-dst-iteration-fusion -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-dst-ownership -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-setexp-fold -mtt-tensix-macro-planner -mtt-tensix-macro-planner-replay -mtt-tensix-optimize-mop-form -mtt-tensix-optimize-capture-rotation -mtt-tensix-optimize-ccmask -mtt-tensix-optimize-interlock-schedule -mtt-tensix-optimize-transp-involution -mtt-tensix-optimize-replay-exec-record -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 6 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-rtl-dump-times "run-boundary drain elided" 3 "rvtt_macro_planner" } }

#define MINMAX_FOUR_FACE_RUNS 1
#include "loadmacro-periodic-minmax-inplace-body.h"
