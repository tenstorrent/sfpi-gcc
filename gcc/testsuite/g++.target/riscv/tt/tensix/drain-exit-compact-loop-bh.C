#include "rtl-lane-reset.h"

// Check that a scalar-only loop tail sizes the exit drain at one SFPNOP.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-drain-schedule -mtt-tensix-optimize-drain-exit -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner drain-backedge: drain=3 pending=1 stream-credit=0 words-per-row=3" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner drain-exit: compensation 3 -> 1 SFPNOPs" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner drain-backedge: loop-carried drain elided; exit compensation 1 SFPNOPs" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 1 } }

#define SELECT_ADDR_MODE 7
#include "drain-backedge-select-loop-body.h"
