// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Near miss under the retired flag (an alias for the general du-chain
// engine since the v1 retirement): every architectural LREG is
// live around the loop or touched by the row -- no free register
// exists (the exp-with-swap shape's terminal state).  Refuse by name.
// { dg-final { scan-rtl-dump "Lreg chain rename refused: regrename-no-free-lreg" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "Lreg chain rename: L" "rvtt_lreg_rename_chains" } }
void ren_nofree ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto k3 = __builtin_rvtt_sfpreadlreg (3);
  auto k4 = __builtin_rvtt_sfpreadlreg (4);
  auto k5 = __builtin_rvtt_sfpreadlreg (5);
  auto k6 = __builtin_rvtt_sfpreadlreg (6);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto t = __builtin_rvtt_sfpand (k1, k2);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      auto q = __builtin_rvtt_sfpmul (p, p, 0);
      auto r = __builtin_rvtt_sfpxor (q, t);
      auto u = __builtin_rvtt_sfpand (k2, k1);
      x = __builtin_rvtt_sfpxor (r, u);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
  __builtin_rvtt_sfpwritelreg (k3, 3);
  __builtin_rvtt_sfpwritelreg (k4, 4);
  __builtin_rvtt_sfpwritelreg (k5, 5);
  __builtin_rvtt_sfpwritelreg (k6, 6);
}
