// Check on WH that a macro-planner SFPLOADMACRO launch payload is priced from the planner's emission records and its replay record hoists to the preheader.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-macro-planner -mtt-tensix-macro-planner-replay -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "planner-derived launch effects: insn \\d+ writes 0x10001 settle 3" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "planner-derived launch effects: insn \\d+ writes 0x10002 settle 3" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoist pricing .loop \\d+.: trips 8, words 4, exec_ilk 4 slots .re-record body, delivery-bound., deliver_body 492, deliver_record 615, record 915, before 615, after 470, benefit 245 .min 60." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoist profitable: modeled benefit 245 >= 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Not hoisting: replay-reissue-latency-unproved" "rvtt_replay" } }

#include "macro-planner-typecast-faces-body.h"

__attribute__((noinline)) void typecast_face_loop_8 ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  for (unsigned face = 0; face != 8; ++face)
    FACE ();
}
