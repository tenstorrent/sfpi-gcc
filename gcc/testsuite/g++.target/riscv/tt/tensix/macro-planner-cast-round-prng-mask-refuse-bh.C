#include "rtl-lane-reset.h"

// Check that a predicated cast-round row without an all-lanes proof stays explicit.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump {all-lanes-proof-missing} "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times {SFPSTOCHRND} 8 } }
// { dg-final { scan-assembler-not {SFPLOADMACRO} } }

#define ROW() do {                                                     \
  auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);     \
  auto cast = __builtin_rvtt_sfpcast (loaded, 0);                     \
  auto rounded = __builtin_rvtt_sfpstochrnd (cast, 1, 0);             \
  __builtin_rvtt_sfpstore (nullptr, rounded, 0, 0, 0, 2, 7);          \
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);                               \
} while (0)

__attribute__((noinline)) void cast_round_prng_masked ()
{
  RVTT_TEST_LANE_RESET ();
  __builtin_rvtt_sfpencc (10, 1);
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
