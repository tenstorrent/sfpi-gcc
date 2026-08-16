// Disjoint configuration state must not suppress formation (WP8): a
// prelude write to a NON-owned configuration destination (15, the LLK
// prelude shape) refuses locally at the writer (row-config-write, a
// config write is never a region member) without invalidating the
// dominating all-lanes proof, and all 32 rows still share ONE
// configuration across the four faces.
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
  __builtin_rvtt_sfpwriteconfig_v (prelude, 15);
  FACE (); FACE (); FACE (); FACE ();
}
