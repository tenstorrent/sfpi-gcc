// Genericity twin: renamed identifiers, varied Dst addresses (16/80,
// in-place store to 16), the other swap sense (min).  The boundary
// placement consumes only the derived calendar and typed effects, so all
// three intra-region boundaries elide exactly as on the canonical shape:
// SFPNOP 12 -> 3.  Launch words differ (the varied addresses encode into
// them) and are deliberately not pinned here.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 6 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-rtl-dump-times "run-boundary drain elided" 3 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=32 runs=4 drain-elided" 1 "rvtt_macro_planner" } }

#include "drain-schedule-renamed-varied-body.h"
