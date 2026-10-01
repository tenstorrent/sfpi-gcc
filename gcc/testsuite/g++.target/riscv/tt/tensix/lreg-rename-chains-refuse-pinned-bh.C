// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Near miss per veto class: the chain value is written into a pinned
// architectural LREG interface inside the row (the zero-length
// protocol marker) -- fixed-LREG protocols never rename.
// { dg-final { scan-rtl-dump "Lreg chain rename refused: regrename-pinned-protocol" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
void renc_pinned ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto t = __builtin_rvtt_sfpmul (k1, k2, 0);
      __builtin_rvtt_sfpwritelreg (t, 3);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      auto r = __builtin_rvtt_sfpxor (p, t);
      auto u = __builtin_rvtt_sfpmul (k2, k1, 0);
      x = __builtin_rvtt_sfpxor (r, u);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}
