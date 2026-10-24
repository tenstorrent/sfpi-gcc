#include "rtl-lane-reset.h"

// An unproved scalar FIFO store must not allow a backedge drain elision.
// Its address/value may be hoisted outside the loop block; refusal can then
// happen at the physical-lane gate before the drain-follower gate.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump {Macro-planner (formation-refusal: cc-enable-unproved|drain-refusal: drain-follower-opaque)} "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "loop-backedge drain elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed:" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times {sw\t[^\n]*,0\([^)]*\)} 1 } }

#define SELECT_ADDR_MODE 7
#define DRAIN_LOOP_TAIL() \
  (*(volatile unsigned *) 0xFFE40000u = 0x02000000u)
#include "drain-backedge-select-loop-body.h"
