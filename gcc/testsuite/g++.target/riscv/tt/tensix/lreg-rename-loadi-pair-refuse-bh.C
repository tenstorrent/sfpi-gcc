// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Check that the rename refuses to split a two-word SFPLOADI pair whose upper half reads the tied lower half.
// { dg-final { scan-rtl-dump "Lreg chain rename refused: regrename-constraint" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-assembler {SFPLOADI\tL([0-7]), 57998, 2\n\tSFPLOADI\tL\1, 11304, 8\t# LV:L\1} } }
void ren_splitpair ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x2c28e28e, 0, 0, -32);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      auto q = __builtin_rvtt_sfpmul (p, p, 0);
      auto r = __builtin_rvtt_sfpxor (q, c);
      auto u = __builtin_rvtt_sfpand (k2, k1);
      x = __builtin_rvtt_sfpxor (r, u);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}
