#include "rtl-lane-reset.h"

// Check that a write to a non-owned configuration destination refuses locally and all 32 rows still share one configuration.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// Four descriptor words plus entry reset, the unowned write and its reset.
// { dg-final { scan-assembler-times "SFPCONFIG" 7 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466693120" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467741696" 16 } }
// { dg-final { scan-assembler-times "TTSETRWC\\t0, 4, 8, 0, 0, 4" 8 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }

#include "macro-planner-typecast-faces-body.h"

__attribute__((noinline)) void typecast_faces_prelude ()
{
  RVTT_TEST_LANE_RESET ();
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto prelude = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, no_increment);
  __builtin_rvtt_sfpwriteconfig_v (prelude, 0, 15);
  // The unowned write changes LaneConfig, so its unknown ROW_MASK cannot
  // supply the full-lane contract needed by the subsequent face regions.
  RVTT_TEST_LANE_RESET ();
  FACE (); FACE (); FACE (); FACE ();
}
