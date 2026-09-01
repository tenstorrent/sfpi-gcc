// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Flag-off control + the census channel: the identical span as the
// all-lanes fire twin, WITHOUT -mtt-tensix-optimize-rename-cc-region.
// The standing blanket refusal fires byte-identically, and the
// dump-only census channel names the RTL-view class the widening
// would prove (the stage-A instrument the wall census greps).
// { dg-final { scan-rtl-dump "Lreg chain rename refused: regrename-cc-span" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "cc-span rtl-view: all-lanes-entry" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-cc-span-region-unproven" "rvtt_lreg_rename_chains" } }
void renc_ccr_census ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto t = __builtin_rvtt_sfpmul (k1, k2, 0);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      {
	auto xg = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	__builtin_rvtt_sfppushc (0);
	__builtin_rvtt_sfpsetcc_v (xg, 0);
	auto zg = __builtin_rvtt_sfpassign_lv (xg, xg);
	__builtin_rvtt_sfpstore (nullptr, zg, 0, 0, 0, 6, 7);
	__builtin_rvtt_sfppopc (0);
      }
      auto r = __builtin_rvtt_sfpxor (p, t);
      auto u = __builtin_rvtt_sfpmul (k2, k1, 0);
      x = __builtin_rvtt_sfpxor (r, u);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}
