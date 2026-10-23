#include "rtl-lane-reset.h"

// { dg-options "-mcpu=tt-bh-tensix -O3 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// Check that SFPABS and SFPCAST mods 1 and 2 stay out of macro rows and a CC-writing SFPIADD refuses at the CC gate.
// { dg-final { scan-rtl-dump-times "row-opaque-effect" 8 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "event-delay-unproven|descriptor-program-unproven|row-not-closed" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "cc-template-unsupported" 4 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "subunit-placement-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner descriptor-word dest=0: 0x7d0000c1" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPABS" 4 } }
// { dg-final { scan-assembler-times "SFPCAST" 8 } }
// { dg-final { scan-assembler "SFPIADD" } }

__attribute__((noinline)) void nearmiss_abs_row ()
{
  RVTT_TEST_LANE_RESET ();
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
#pragma GCC unroll 4
  for (int row = 0; row < 4; ++row)
    {
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
      auto m = __builtin_rvtt_sfpabs (a, 1);
      __builtin_rvtt_sfpstore (nullptr, m, 0, 0, 0, 4, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

__attribute__((noinline)) void unproven_cast_mod1_row ()
{
  RVTT_TEST_LANE_RESET ();
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
#pragma GCC unroll 4
  for (int row = 0; row < 4; ++row)
    {
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
      auto c = __builtin_rvtt_sfpcast (a, 1);
      __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 4, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

__attribute__((noinline)) void unproven_cast_mod2_row ()
{
  RVTT_TEST_LANE_RESET ();
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
#pragma GCC unroll 4
  for (int row = 0; row < 4; ++row)
    {
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
      auto c = __builtin_rvtt_sfpcast (a, 2);
      __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 4, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

__attribute__((noinline)) void ccwrite_iadd_row ()
{
  RVTT_TEST_LANE_RESET ();
#pragma GCC unroll 4
  for (int row = 0; row < 4; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);
      auto s = __builtin_rvtt_sfpiadd_v (b, a, 0);	/* CC_LT0 */
      __builtin_rvtt_sfpstore (nullptr, s, 0, 0, 0, 4, 7);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
