// Check that a single-run region keeps its exit drain (SFPNOP 3).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-rtl-dump-not "drain elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "drain-elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }

#include "loadmacro-periodic-minmax-inplace-body.h"
