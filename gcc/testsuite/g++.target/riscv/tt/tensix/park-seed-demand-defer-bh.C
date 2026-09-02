// PARK-SEED COMPOSITION demand gate: a CC-restore loop with
// THREE OR MORE in-region invariant constants defers wholesale
// (in-region-demand) — that demand puts the loop in the
// pressure-arbitrated regime where the later placement authorities
// (lut-select coefficient placement, the walk's audited parks) own the
// 8-LREG headroom an early depth-zero keep would pin
// (sigmoid-appx-tree anatomy: three early keeps pushed the lut-select
// coefficient placement to lut-coefficient-pressure and the 5-word LUT
// row decayed, 29861 -> 43447 cycles).  The depth-zero candidate here
// is NOT kept.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "defers wholesale: in-region-demand .3 in-region constants" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "hoist deferred: residency-walk-ordering" 4 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "depth-zero-hoist-dominant" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }

void demand_defers_wholesale (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto scale = __builtin_rvtt_sfpxloadi (nullptr, 0x3f2a7c11, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, scale, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3da6b50b, 0, 0, 31);
      auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e194af5, 0, 0, 31);
      auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e7b15b5, 0, 0, 31);
      auto t = __builtin_rvtt_sfpmul (x, c0, 0);
      t = __builtin_rvtt_sfpmul (t, c1, 0);
      t = __builtin_rvtt_sfpmul (t, c2, 0);
      x = __builtin_rvtt_sfpassign_lv (x, t);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
