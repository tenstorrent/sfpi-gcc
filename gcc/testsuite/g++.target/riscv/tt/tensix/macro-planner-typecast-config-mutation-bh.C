// Config-mutation-between-faces refusal (WP8 step 4): a typed write to
// a planner-owned configuration destination between two faces refuses
// every formation in the function (function-global ownership), keeping
// the bytes explicit.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "config-ownership-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler "SFPSTOCHRND" } }
// { dg-final { scan-assembler "TTSETRWC" } }

#include "macro-planner-typecast-faces-body.h"

__attribute__((noinline)) void typecast_faces_mutated ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  FACE ();
  auto knob = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, no_increment);
  __builtin_rvtt_sfpwriteconfig_v (knob, 4);
  FACE ();
}
