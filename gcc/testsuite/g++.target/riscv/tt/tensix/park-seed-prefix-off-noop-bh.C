// PARK-SEED COMPOSITION flag-off control: without
// -mtt-tensix-optimize-park-ordering the pre-CC-prefix source of
// park-seed-prefix-kept-bh.C takes the identical early hoists and
// prints NO ordering lines of either name -- the refinement's kept
// class makes flag-on converge to this flag-off placement on the
// prefix, never the reverse.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "depth-zero-hoist-dominant" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "hoist deferred: residency-walk-ordering" "rvtt_invariant" } }

void prefix_kept_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto magic = __builtin_rvtt_sfpxloadi (nullptr, 0x4b000000, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, magic, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_varied_prefix (void)
{
  auto north = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned step = 0; step != 12; ++step)
    {
      auto scale = __builtin_rvtt_sfpxloadi (nullptr, 0x3e99f042, 0, 0, 31);
      north = __builtin_rvtt_sfpmul (north, scale, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (north, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (north, 2);
}
