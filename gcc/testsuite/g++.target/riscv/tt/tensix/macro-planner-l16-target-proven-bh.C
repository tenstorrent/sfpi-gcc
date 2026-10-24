#include "rtl-lane-reset.h"

// Check that SFPABS is not admitted to the LReg16 staging register while the same-shape SFPCAST row keeps it.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: subunit-placement-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=1 runs=1 config=preheader lane-proof=materialized-enable" 1 "rvtt_macro_planner" } }
// The SFPCAST control keeps the LReg16 staging: sequence word
// 0x4b000044 (template0 and store both vd16-flagged).
// { dg-final { scan-assembler-times "SFPABS" 1 } }
// { dg-final { scan-rtl-dump-times "descriptor-word dest=4: 0x4b000044" 1 "rvtt_macro_planner" } }

__attribute__((noinline)) void abs_vd_direct_loop ()
{
  RVTT_TEST_LANE_RESET ();
  for (int row = 0; row < 32; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 4, 0, 0, 7);
      auto r = __builtin_rvtt_sfpabs (v, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 4, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

__attribute__((noinline)) void cast_l16_control_loop ()
{
  RVTT_TEST_LANE_RESET ();
  for (int row = 0; row < 32; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto r = __builtin_rvtt_sfpcast (v, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
