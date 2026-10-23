#include "rtl-lane-reset.h"

// Check that the inter-row drain applies with different names, row count, Dst address and shift amount.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=3 rows=4" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 12 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-not "SFPSHFT" } }
// { dg-final { scan-assembler-not "SFPCAST" } }

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

__attribute__((noinline)) void exponent_hoist_quad ()
{
  RVTT_TEST_LANE_RESET ();
  STANZA ();
  STANZA ();
  STANZA ();
  STANZA ();
}

#undef STANZA
