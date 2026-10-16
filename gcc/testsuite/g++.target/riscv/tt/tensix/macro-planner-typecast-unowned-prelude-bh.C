// Check that a write to a non-owned configuration destination refuses locally and all 32 rows still share one configuration.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466693120" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467741696" 16 } }
// { dg-final { scan-assembler-times "TTSETRWC\\t0, 4, 8, 0, 0, 4" 8 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }

#include "macro-planner-typecast-faces-body.h"

__attribute__((noinline)) void typecast_faces_prelude ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto prelude = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, no_increment);
  __builtin_rvtt_sfpwriteconfig_v (prelude, 0, 15);
  FACE (); FACE (); FACE (); FACE ();
}
