#include "rtl-lane-reset.h"

// Check that unproven integer-row events (fresh-dest or mod-2 SFPMUL24, stochastic SFPCAST, shared store producer) stay explicit.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-assembler "SFPMUL24" } }
// { dg-final { scan-assembler "SFPCAST\[^\n\]*, 1" } }
// { dg-final { scan-assembler "SFPSTORE" } }

__attribute__((noinline)) void fresh_dest_factor_rows ()
{
  RVTT_TEST_LANE_RESET ();
#pragma GCC unroll 8
  for (int row = 0; row < 8; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
      a = __builtin_rvtt_sfpcast (a, 3);
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);
      b = __builtin_rvtt_sfpcast (b, 3);
      /* Fresh destination with both factors live past the multiply: no
         register tying makes the result the launch VD, so the SFPMUL24
         has no launch-VD realization and stays an explicit issue.  */
      auto c = __builtin_rvtt_sfpmul24 (a, b, 0);
      c = __builtin_rvtt_sfpiadd_v (c, a, 4);
      c = __builtin_rvtt_sfpiadd_v (c, b, 4);
      __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 4, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

__attribute__((noinline)) void stochastic_cast_rows ()
{
  RVTT_TEST_LANE_RESET ();
#pragma GCC unroll 8
  for (int row = 0; row < 8; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
      a = __builtin_rvtt_sfpcast (a, 1);	/* stochastic: unproven */
      __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 4, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

__attribute__((noinline)) unsigned dual_consumer_rows ()
{
  RVTT_TEST_LANE_RESET ();
  unsigned acc = 0;
#pragma GCC unroll 8
  for (int row = 0; row < 8; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
      a = __builtin_rvtt_sfpcast (a, 3);
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);
      b = __builtin_rvtt_sfpcast (b, 3);
      b = __builtin_rvtt_sfpiadd_v (b, a, 4);
      /* The producer feeds BOTH the store and a second in-row consumer:
	 the sole-producer proof must refuse the hosted store source.  */
      auto probe = __builtin_rvtt_sfpiadd_v (a, b, 4);
      __builtin_rvtt_sfpstore (nullptr, b, 0, 0, 0, 4, 7);
      __builtin_rvtt_sfpstore (nullptr, probe, 128, 0, 0, 4, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  return acc;
}
