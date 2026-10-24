#include "rtl-lane-reset.h"

// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// Check that a Dst stride of 4, which the SETC16 address-modifier program cannot absorb, refuses formation.
// { dg-final { scan-rtl-dump "Macro-planner formation-refusal: stride-not-absorbed" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "derived-stride-absorption-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not {SFPCONFIG[ \t]+([0-9]|1[0-4]),} } }
// { dg-final { scan-assembler "SFPSWAP" } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

__attribute__((noinline)) void pelican_floor_rows_stride4 ()
{
  RVTT_TEST_LANE_RESET ();
#pragma GCC unroll 8
  for (int row = 0; row < 32; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto v = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 3);
      auto cst = __builtin_rvtt_sfpreadlreg (11);
      auto pair = __builtin_rvtt_sfpswap (v, cst, 1);
      auto r = __builtin_rvtt_sfpselect2 (pair, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 64, 0, 0, 0, 3);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
}
