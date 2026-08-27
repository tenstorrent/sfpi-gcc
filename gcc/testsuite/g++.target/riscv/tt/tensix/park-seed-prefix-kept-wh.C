// WH arm of park-seed-prefix-kept-bh.C: the pre-CC-prefix keep is
// arch-independent like the deferral it refines (flag-and-position
// gated only).  One function (the BH file carries the renamed twin
// pair).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -fdump-tree-rvtt_invariant-details -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "hoist kept under park-ordering: depth-zero-hoist-dominant" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "hoist deferred: residency-walk-ordering" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }

void prefix_kept_fire_wh (void)
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
