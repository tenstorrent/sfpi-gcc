// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename -fdump-rtl-rvtt_lreg_rename_chains-details" }
// The rename engine's payoff decoupling, pinned at the v1 retirement: the
// retired single-shape pass refused this stall-free row by
// rename-no-stall-decrease (payoff smuggled into admission); the
// general engine the retired flag now requests renames whenever the
// chain is legal and the whole-row cost is no worse (equal counts
// accepted -- strict acceptance rejects only regressions).
// { dg-final { scan-rtl-dump "Lreg chain rename: L\\d+ -> L\\d+" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-cost-regressed" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
void ren_nostall ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto t = __builtin_rvtt_sfpand (k1, k2);
      auto r = __builtin_rvtt_sfpxor (x, t);
      auto u = __builtin_rvtt_sfpand (k2, k1);
      x = __builtin_rvtt_sfpxor (r, u);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}
