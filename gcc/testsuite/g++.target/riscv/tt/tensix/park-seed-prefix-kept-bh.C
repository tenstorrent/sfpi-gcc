// Check that an invariant immediate before the first CC statement keeps the early hoist under -mtt-tensix-optimize-park-ordering.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -fdump-tree-rvtt_invariant-details -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "hoist kept under park-ordering: depth-zero-hoist-dominant" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "hoist deferred: residency-walk-ordering" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "peeled first iteration" "rvtt_prgm_const" } }

void prefix_kept_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto magic = __builtin_rvtt_sfpxloadi (nullptr, 0x4b000000, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, magic, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
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
      __builtin_rvtt_sfpsetcc (north, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (north, 2);
}
