// Near miss (delivered word): a scalar memory store in the loop tail is
// the instruction-FIFO delivery idiom -- a word this proof cannot
// classify -- so the backedge elision refuses (follower-opaque) and the
// in-body drain stays byte-identically.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner drain-refusal: drain-follower-opaque" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "loop-backedge drain elided" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }

#define SELECT_ADDR_MODE 7
#define DRAIN_LOOP_TAIL() \
  (*(volatile unsigned *) 0xFFE40000u = 0x02000000u)
#include "drain-backedge-select-loop-body.h"
