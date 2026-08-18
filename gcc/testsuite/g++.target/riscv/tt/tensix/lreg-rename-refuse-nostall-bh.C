// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename -fdump-rtl-rvtt_lreg_rename-details" }
// Near miss: the row carries no audited-latency stall -- a rename has
// nothing to pay for.  Refuse by name, bytes unchanged.
// { dg-final { scan-rtl-dump "Lreg rename refused: rename-no-stall-decrease" "rvtt_lreg_rename" } }
// { dg-final { scan-rtl-dump-not "Lreg rename: chain" "rvtt_lreg_rename" } }
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
