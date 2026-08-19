// The production TTNN Where delivery shape: the compact loop with the
// typed architectural face advance in the loop tail.  The advance is a
// never-absorbed launch-latched pure-RWC word pair (AIC_RWC_STEP
// contract) and earns two slots of stream credit ahead of the backedge
// followers; the elision proves as in drain-backedge-compact-loop-bh.C.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner drain-backedge: drain=3 pending=1 stream-credit=2 words-per-row=3" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner drain-schedule: loop-backedge drain elided \\(drain=1 stream-credit=2\\)" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable drain-backedge" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }

#define SELECT_ADDR_MODE 7
#define DRAIN_LOOP_TAIL() __builtin_rvtt_ttdstface ()
#include "drain-backedge-select-loop-body.h"
