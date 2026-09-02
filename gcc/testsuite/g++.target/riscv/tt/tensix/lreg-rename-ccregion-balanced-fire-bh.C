// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -mtt-tensix-optimize-rename-cc-region -fdump-rtl-rvtt_lreg_rename_chains-details" }
// The BALANCED-FRAMES fire (the cc-span RTL-view widening, arm
// F): the colliding chain lives INSIDE an outer predicated frame and
// its span straddles a complete NESTED pushc/setcc/popc frame -- every
// interior lane-enable state a proven subset of the span-entry mask,
// the popc restoring it exactly -- so the chain renames with NO
// all-lanes entry proof (the outer frame's depth-0 refinement makes
// the entry deliberately unprovable).  The same body pins two
// fail-closed arms: a span whose kill-close sits past the outer
// depth-0 SETCC refuses refuse-end-mask, and a span crossing the outer
// frame's all-lanes ENCC reset refuses refuse-entry-unproven.
// { dg-final { scan-rtl-dump "cc-span rtl-view: balanced-frames" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "cc-span rtl-view: refuse-end-mask" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "cc-span rtl-view: refuse-entry-unproven" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-times "Lreg chain rename refused: regrename-cc-span-region-unproven" 2 "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "Lreg chain rename: renames=2" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
void renc_ccr_balanced ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto g = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (g, 0);
      auto t = __builtin_rvtt_sfpmul (k1, k2, 0);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      /* The nested frame the balanced chain span straddles.  */
      {
	auto xg = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	__builtin_rvtt_sfppushc (0);
	__builtin_rvtt_sfpsetcc_v (xg, 0);
	auto zg = __builtin_rvtt_sfpassign_lv (xg, xg);
	__builtin_rvtt_sfpstore (nullptr, zg, 0, 0, 0, 6, 7);
	__builtin_rvtt_sfppopc (0);
      }
      auto r = __builtin_rvtt_sfpor (p, t);
      auto u = __builtin_rvtt_sfpmul (k2, k1, 0);
      x = __builtin_rvtt_sfpor (r, u);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}
