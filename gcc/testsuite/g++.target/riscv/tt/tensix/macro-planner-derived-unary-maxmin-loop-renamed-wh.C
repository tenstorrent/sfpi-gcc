// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// WH renamed/varied twin of the derived unary max/min loop: different
// function name, a different constant register (L11), a different Dst
// address, the opposite operand layout, and the other CPU.  The
// derived calendar's stride absorption is a PER-CPU proven envelope:
// the WH launch auto-increment/dual-slot machinery is the open WH
// Dst-advance frontier (FINDING-wh-dst-autoincr-fresh-maxmin.md; the
// laneR1 wh-onma evidence shows a WH-formed absorbed-stride calendar
// returning position-shuffled tiles after the first), so on WH every
// grouping candidate refuses by name and the rows stay byte-identical
// explicit code.  The BH twin (macro-planner-derived-unary-maxmin-
// loop-bh.C) forms.
// { dg-final { scan-rtl-dump "sequence-derivation-hazard" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "derived-stride-absorption-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// (The default replay mechanism records the refused rows: one literal
// SFPSWAP body plus TTREPLAY re-issues.)
// { dg-final { scan-assembler "SFPSWAP" } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

__attribute__((noinline)) void pelican_floor_rows ()
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
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
