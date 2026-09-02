// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Near miss under the retired flag (an alias for the general du-chain
// engine since the v1 retirement): the candidate's register is
// shared with an earlier chain (the wall) but has no later in-row
// writer -- the definition is live around the backedge (next trip's
// reader consumes it) and no rename can prove the value dies in-row.
// Refuse by name.
// { dg-final { scan-rtl-dump "Lreg chain rename refused: regrename-chain-open" "rvtt_lreg_rename_chains" } }
void ren_boundary ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  auto t2 = __builtin_rvtt_sfpand (k1, k2);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto r = __builtin_rvtt_sfpxor (x, t2);
      auto t1 = __builtin_rvtt_sfpmul (k2, k1, 0);
      auto y = __builtin_rvtt_sfpxor (r, t1);
      t2 = __builtin_rvtt_sfpand (k1, k2);
      auto p = __builtin_rvtt_sfpmul (y, y, 0);
      x = __builtin_rvtt_sfpmul (p, p, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}
