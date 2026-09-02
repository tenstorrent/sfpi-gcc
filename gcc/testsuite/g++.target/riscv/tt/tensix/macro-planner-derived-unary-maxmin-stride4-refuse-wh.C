// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// Near-miss twin of macro-planner-derived-unary-maxmin-loop-renamed-wh:
// identical shape, but the row's Dst stride is 4.  The WH per-CPU
// absorption refusal is discharged (the dual-slot machinery was the
// adjudicated bug), but the proven SETC16
// address-modifier program still covers ONLY Dst += 2
// (rvtt-macro-tables.cc addr_mod_program), so the stride cannot be
// absorbed, the schedule keeps its explicit separator, and formation
// refuses by name.  The rows stay byte-identical explicit code.
// { dg-final { scan-rtl-dump "Macro-planner formation-refusal: stride-not-absorbed" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "derived-stride-absorption-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler "SFPSWAP" } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

__attribute__((noinline)) void pelican_floor_rows_stride4 ()
{
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
