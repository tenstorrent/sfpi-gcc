// { dg-options "-mcpu=tt-qsr32-tensix -O3 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// QSR has no simulator specification for SFPIADD: its effect claims keep
// the refusing defaults, so the row stays an opaque boundary.
// { dg-final { scan-rtl-dump "row-opaque-effect" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

__attribute__((noinline)) void iadd_row_qsr ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
#pragma GCC unroll 4
  for (int row = 0; row < 4; ++row)
    {
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);
      auto s = __builtin_rvtt_sfpiadd_v (b, a, 4);
      __builtin_rvtt_sfpstore (nullptr, s, 0, 0, 0, 4, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
