// Near-miss refusal (WP8 desc-program audit): the unary shift/cast
// program's CRAQ-validated envelope covers only a uniform data mode
// across the row's Dst accesses; a load/store mode mismatch is
// structurally identical but outside the envelope and refuses by name,
// keeping the bytes explicit.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "descriptor-program-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler "SFPSHFT" } }
// { dg-final { scan-assembler "TTINCRWC" } }

__attribute__((noinline)) void staged_mode_mismatch (unsigned iterations)
{
  for (unsigned row = 0; row < iterations; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
      auto converted = __builtin_rvtt_sfpcast (shifted, 0);
      __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 2, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
