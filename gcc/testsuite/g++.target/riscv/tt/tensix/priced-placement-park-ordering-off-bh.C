// Check that without -mtt-tensix-optimize-priced-placement the in-region-demand cut defers the whole loop.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -fdump-tree-rvtt_invariant-details -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "defers wholesale: in-region-demand .3 in-region constants" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "hoist deferred: residency-walk-ordering" 4 "rvtt_invariant" } }

void park_priced_keep (void)
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto d0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb8aa3b, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, d0, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      auto g0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, g0, 0);
      auto g1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f21aa52, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, g1, 0);
      auto g2 = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, g2, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
