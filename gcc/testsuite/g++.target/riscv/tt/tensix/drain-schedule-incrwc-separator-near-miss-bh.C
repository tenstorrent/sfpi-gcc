// Check that a single TTINCRWC face advance earns no slot credit, so every boundary
// refuses (drain-lreg-overlap) and keeps its drain.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-assembler-times "SFPNOP" 12 } }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-refusal: drain-lreg-overlap" 3 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "drain elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "drain-elided" "rvtt_macro_planner" } }

#define MINMAX_FOUR_FACE_RUNS 1
#define MINMAX_FACE_ADVANCE() __builtin_rvtt_ttincrwc (4, 7, 0, 0)
#include "loadmacro-periodic-minmax-inplace-body.h"
