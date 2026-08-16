// Encodability refusal direction (WP8): a run-time shift amount cannot
// pack the template imm12 field, so the region refuses by name and the
// explicit rows stay byte-identical.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "descriptor-encoding-failed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-times "SFPSHFT" 1 } }
// { dg-final { scan-assembler-times "SFPCAST" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 1 } }

#if __riscv_xtttensixwh
constexpr unsigned no_increment = 3;
#else
constexpr unsigned no_increment = 7;
#endif

__attribute__((noinline)) void staged_loop_dynamic (unsigned iterations,
						    unsigned amount)
{
  for (unsigned row = 0; row < iterations; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0,
					    no_increment);
      auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, amount,
					       0, 0, 0);
      auto converted = __builtin_rvtt_sfpcast (shifted, 0);
      __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0,
			       no_increment);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
