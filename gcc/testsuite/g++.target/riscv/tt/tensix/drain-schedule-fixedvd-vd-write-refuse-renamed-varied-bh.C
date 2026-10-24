#include "rtl-lane-reset.h"

// Check the drain-follower-vd-write refusal with renamed symbols, another address and shift.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner drain-refusal: drain-follower-vd-write" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=3 rows=8" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "drain elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "drain-elided" "rvtt_macro_planner" } }

#define STANZA()                                                              \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto picked = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);          \
      auto moved = __builtin_rvtt_sfpshft_i (nullptr, picked, -30, 0, 0, 0);  \
      auto widened = __builtin_rvtt_sfpcast (moved, 0);                       \
      __builtin_rvtt_sfpstore (nullptr, widened, 0, 0, 0, 4, 7);              \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void exponent_walk_hex ()
{
  RVTT_TEST_LANE_RESET ();
  STANZA ();
  STANZA ();
  STANZA ();
  STANZA ();
  __builtin_rvtt_ttincrwc (0, 4, 0, 0);
  STANZA ();
  STANZA ();
  STANZA ();
  STANZA ();
}

#undef STANZA
