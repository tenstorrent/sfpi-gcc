// Renamed-varied twin of replay-hoist-planner-launch-fire-bh.C: every
// identifier renamed, sixteen trips instead of eight.  The pricing keys
// on the planner's own emission records and the loop's provable trip
// count, never on names or shapes: 16 * (615 - 470) - 915 = +1405.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-macro-planner -mtt-tensix-macro-planner-replay -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "planner-derived launch effects: insn \\d+ writes 0x10001 settle 3" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoist pricing .loop \\d+.: trips 16, words 4, exec_ilk 4 slots .re-record body, delivery-bound., deliver_body 492, deliver_record 615, record 915, before 615, after 470, benefit 1405 .min 60." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoist profitable: modeled benefit 1405 >= 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Not hoisting: replay-reissue-latency-unproved" "rvtt_replay" } }

#include "macro-planner-typecast-faces-body.h"

__attribute__((noinline)) void sixteen_panel_walk (void)
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  for (unsigned panel = 0; panel != 16; ++panel)
    FACE ();
}
