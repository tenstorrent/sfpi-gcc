// Trip-weight profitability (WP8): a proven three-trip loop cannot
// amortize the configuration prefix, so the loop-body region refuses
// unprofitable under the exact trip weight (the profile ratio is
// exactly 3).  Bytes stay explicit.  (Complete unrolling is disabled so
// the loop shape survives to the planner.)
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fdisable-tree-cunrolli -fdisable-tree-cunroll -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "region: rows=1 row-len=4 runs=1 stride=2 loop=yes" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "formation-refusal: unprofitable .trip-weight=" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// (The two-trip loop may be unrolled; scan for presence.)
// { dg-final { scan-assembler "SFPSHFT" } }
// { dg-final { scan-assembler "TTINCRWC" } }

#if __riscv_xtttensixwh
constexpr unsigned no_increment = 3;
#else
constexpr unsigned no_increment = 7;
#endif

__attribute__((noinline)) void three_trips ()
{
#pragma GCC unroll 1
  for (unsigned row = 0; row < 3; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0,
					    no_increment);
      auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31,
					       0, 0, 0);
      auto converted = __builtin_rvtt_sfpcast (shifted, 0);
      __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0,
			       no_increment);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
