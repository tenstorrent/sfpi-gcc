// Check that a depth-zero candidate keeps the early hoist while an in-region one in the same loop defers to the walk.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -fdump-tree-rvtt_invariant-details -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "hoist kept under park-ordering: depth-zero-hoist-dominant" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "hoist deferred: residency-walk-ordering" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x40490fdb .loop class" 1 "rvtt_prgm_const" } }

void mixed_prefix_and_postcc (void)
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto magic = __builtin_rvtt_sfpxloadi (nullptr, 0x4b400000, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, magic, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      __builtin_rvtt_sfppopc (0);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
