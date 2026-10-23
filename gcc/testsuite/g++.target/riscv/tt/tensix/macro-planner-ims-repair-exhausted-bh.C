#include "rtl-lane-reset.h"

// Check that the IMS repair search refuses when every reduced hosted set still fails.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-ims -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: ims-repair" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed:" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not {SFPCONFIG[ \t]+([0-9]|1[0-4]),} } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfpcast (a, 3);                                      \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      b = __builtin_rvtt_sfpcast (b, 3);                                      \
      auto hi = __builtin_rvtt_sfpmul24 (a, b, 1);                            \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      b = __builtin_rvtt_sfpshft_i (nullptr, b, -23, 0, 0, 0);                \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpcast (hi, 3);                                    \
      __builtin_rvtt_sfpstore (nullptr, hi, 0, 0, 0, 4, 7);                   \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void exhausted_rows ()
{
  RVTT_TEST_LANE_RESET ();
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
#undef ROW
