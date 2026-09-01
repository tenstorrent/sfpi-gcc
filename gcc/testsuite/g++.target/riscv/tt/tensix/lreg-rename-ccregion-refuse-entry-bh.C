// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -mtt-tensix-optimize-rename-cc-region -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Near miss under the widening flag: the all-lanes fire body with an
// entry-state spoiler -- a non-all-lanes SFPENCC on the preheader
// path.  The kill-modeling backward walk cannot prove the span entry
// all-lanes (one predecessor path's last CC event is an unproven mask
// write) and the widened arm refuses by its own name.
// { dg-final { scan-rtl-dump "cc-span rtl-view: refuse-entry-unproven" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "Lreg chain rename refused: regrename-cc-span-region-unproven" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
void renc_ccr_entry ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  /* Entry spoiler: every path into the loop carries an unproven
     lane-enable state.  */
  __builtin_rvtt_sfpencc (0, 10);
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
