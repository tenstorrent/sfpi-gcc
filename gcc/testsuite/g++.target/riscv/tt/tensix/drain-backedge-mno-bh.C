// Default-off twin: with -mno-tt-tensix-optimize-drain-schedule the
// loop keeps its in-body drain and no backedge line appears.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mno-tt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-not "Macro-planner drain-backedge" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "exit compensation" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }

#define SELECT_ADDR_MODE 7
#include "drain-backedge-select-loop-body.h"
