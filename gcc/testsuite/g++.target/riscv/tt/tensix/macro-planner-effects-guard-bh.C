// { dg-options "-mcpu=tt-bh-tensix -O3 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// Near-miss guards for the effect-attributes work: the refusing default
// must still hold everywhere it was not explicitly proven.
//   - an UNattributed neighboring pattern (SFPABS) inside a row;
//   - SFPCAST mod 1 (stochastic rounding: advances the PRNG, outside
//     the effect vocabulary);
//   - SFPCAST mod 2 (the documented BH cast-as-ABS hardware bug).
// Each row dissolves at the opaque member; a CC-writing SFPIADD
// (CC_LT0) instead refuses at the CC write by name.  The D3 latency
// audit gave SFPABS mod 1 a Layer-1 effect set (the rows then refused
// one gate later at the Layer-4 tables), and the lane CZ enumerated
// vocabulary admission PROVED the ABS descriptor program
// (differential vs the pinned simulators, macro-planner-unary-*), so
// the abs rows now legitimately FORM -- this guard keeps watching the
// still-unproven neighbors: both stochastic/bug cast mods stay
// opaque, and the CC-writing accumulate still refuses at the CC gate
// by name, never forming.
// { dg-final { scan-rtl-dump-times "row-opaque-effect" 8 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "event-delay-unproven|descriptor-program-unproven|row-not-closed" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "cc-template-unsupported" 4 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=4 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x7d0000c1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPABS" } }
// { dg-final { scan-assembler-times "SFPCAST" 8 } }
// { dg-final { scan-assembler "SFPIADD" } }

__attribute__((noinline)) void nearmiss_abs_row ()
{
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
