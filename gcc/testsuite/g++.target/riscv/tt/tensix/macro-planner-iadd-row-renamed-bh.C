// { dg-options "-mcpu=tt-bh-tensix -O3 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// Renamed/varied twin of macro-planner-iadd-row-bh.C: different function
// name, the subtract imod (2SCOMP | CC_NONE = 6), different Dst
// addresses, and a different data mode.  Behavior must be derived from
// the typed effects, never from names or constants: identical
// classification to the add twin.
// { dg-final { scan-rtl-dump-not "row-opaque-effect" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "row-not-closed" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner region: rows=4 row-len=7" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-program-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

__attribute__((noinline)) void banana_split_rows ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
#pragma GCC unroll 4
  for (int row = 0; row < 4; ++row)
    {
      auto left = __builtin_rvtt_sfpload (nullptr, 128, 0, 0, 2, 7);
      auto li = __builtin_rvtt_sfpcast (left, 3);
      auto right = __builtin_rvtt_sfpload (nullptr, 192, 0, 0, 2, 7);
      auto ri = __builtin_rvtt_sfpcast (right, 3);
      auto d = __builtin_rvtt_sfpiadd_v (ri, li, 6);
      auto dm = __builtin_rvtt_sfpcast (d, 3);
      __builtin_rvtt_sfpstore (nullptr, dm, 128, 0, 0, 2, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
