#include "rtl-lane-reset.h"

// Check that the in-body drain of a compact loop is elided on the backedge and one
// SFPNOP 3 drain lands after the loop.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner drain-backedge: drain=3 pending=1 stream-credit=0 words-per-row=3" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner drain-schedule: loop-backedge drain elided \\(drain=1 stream-credit=0\\)" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "exit compensation 3 SFPNOPs" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable drain-backedge" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define SELECT_ADDR_MODE 7
#include "drain-backedge-select-loop-body.h"
