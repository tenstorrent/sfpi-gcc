#include "rtl-lane-reset.h"

// Check that window-pairing prices a row with a store-source staging copy into LReg16 at one NOP.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-window-pairing -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "staging=copy" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=2 rows=8" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner window-pairing: interrow-drain 2 -> 1 rows=8 bound=window-pairing-lreg-overlap" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "window-pairing-refusal: window-pairing-footprint-opaque" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 9 } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      auto hi = __builtin_rvtt_sfpmul24 (a, b, 1);                            \
      auto lo = __builtin_rvtt_sfpmul24 (a, b, 0);                            \
      hi = __builtin_rvtt_sfpshft_i (nullptr, hi, 23, 0, 0, 0);               \
      lo = __builtin_rvtt_sfpiadd_v (lo, hi, 4);                              \
      lo = __builtin_rvtt_sfpcast (lo, 3);                                    \
      __builtin_rvtt_sfpstore (nullptr, lo, 0, 0, 0, 4, 6);                   \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void limb2_cast_rows ()
{
  RVTT_TEST_LANE_RESET ();
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
