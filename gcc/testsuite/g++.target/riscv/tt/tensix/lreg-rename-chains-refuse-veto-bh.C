// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Near miss per veto class: the colliding definition writes CC (the
// CC-setting integer add) -- a vetoed typed effect; the chain refuses
// and nothing is renamed in this row.
// { dg-final { scan-rtl-dump "Lreg chain rename refused: regrename-effect-veto" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "Lreg chain rename: L" "rvtt_lreg_rename_chains" } }
void renc_veto ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned row = 0; row != 20; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      auto t = __builtin_rvtt_sfpiadd_i (nullptr, k1, 10, 0, 0, 0);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      auto r = __builtin_rvtt_sfpxor (p, t);
      auto u = __builtin_rvtt_sfpmul (k2, k1, 0);
      x = __builtin_rvtt_sfpxor (r, u);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}
