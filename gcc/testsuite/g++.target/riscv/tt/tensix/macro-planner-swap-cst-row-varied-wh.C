// { dg-options "-mcpu=tt-wh-tensix -O3 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// WH renamed/varied twin of the constant-LREG swap row: different
// function names, a different constant register (L10 = vConst1), a
// different swap mod (2: SUBVEC_MIN01_MAX23), and different addresses.
// Same typed classification -- closed region, named downstream refusal,
// no formation.
// { dg-final { scan-rtl-dump-not "row-opaque-effect" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "row-not-closed" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner region: rows=4 row-len=3" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "descriptor-encoding-failed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

__attribute__((noinline)) void kumquat_clamp_rows ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
#pragma GCC unroll 4
  for (int row = 0; row < 4; ++row)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 32, 0, 0, 0, 3);
      auto one = __builtin_rvtt_sfpreadlreg (10);
      auto pair = __builtin_rvtt_sfpswap (v, one, 2);
      auto r = __builtin_rvtt_sfpselect2 (pair, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 32, 0, 0, 0, 3);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
