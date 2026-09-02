// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Retirement witness (the calculate_i0 residual class): a 32-bit
// constant materialized in-row is a two-word SFPLOADI pair -- USHORT
// lower half, then the mod0-8 UPPER half-word merge reading the tied
// live value.  The RETIRED single-shape pass fired here and split the
// pair by whole-pattern rename (upper half merged into a dead
// register's garbage lower half -- wrong code, adjudicated at
// its committed stream).  The general engine the retired flag now
// requests cannot split a "0"-tied lv family: the destructive edit
// fails constraint re-recognition and refuses by name, and the pair
// stays intact on one register in the final stream (the lower half's
// register equals the upper half's register AND its tied lv source).
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
