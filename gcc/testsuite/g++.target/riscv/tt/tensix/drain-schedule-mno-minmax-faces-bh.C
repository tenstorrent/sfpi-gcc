#include "rtl-lane-reset.h"

// Check that -mno-tt-tensix-optimize-drain-schedule keeps a drain after every run (SFPNOP 12).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mno-tt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-assembler-times "SFPNOP" 12 } }
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 6 } }
// { dg-final { scan-rtl-dump-not "drain elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "drain-elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=32 runs=4" 1 "rvtt_macro_planner" } }

#define MINMAX_FOUR_FACE_RUNS 1
#include "loadmacro-periodic-minmax-inplace-body.h"
