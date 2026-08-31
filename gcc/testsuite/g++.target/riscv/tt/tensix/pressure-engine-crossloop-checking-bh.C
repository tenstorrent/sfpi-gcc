// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -fchecking=2 -fdump-tree-rvtt_crossloop" }
// Item-#10 verdict-identity twin: the crossloop greedy selector now
// prices each candidate through the unified engine's incremental
// per-loop profile (rvtt_loop_pressure, rvtt-pressure.cc) instead of
// a full proof re-run per candidate; under -fchecking every
// incremental verdict is recomputed through the full proof AND the
// verbatim legacy loop counter and asserted equal.  The hoists must
// fire exactly as in crossloop-hoist-bh.C, plus the checking leg.
// { dg-final { scan-tree-dump-times "crossloop-hoist: hoisted across loop" 2 "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "refused" "rvtt_crossloop" } }

#define XLH_VAL_C0 0x3e4b1a3d
#define XLH_VAL_C1 0x3f2e8ba3
#include "crossloop-hoist-body.h"
