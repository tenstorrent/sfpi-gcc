// PLACEMENT-ARBITER park-ordering priced keep (the placement arbiter; shadow leg =
// priced-placement-park-ordering-shadow-bh.C, same body): three
// in-region invariant constants trip the legacy `in_region >= 3'
// demand cut -- the measured local optimum whose named successor is
// this arbiter -- so the legacy verdict defers the loop WHOLESALE,
// including its depth-zero candidate whose hoist is a mask-exact free
// code motion.  The priced spelling asks the pressure engine the
// question the cut approximates: keeps plus in-region parks fit the
// LREG file here, so there is no contention for the later authorities
// to arbitrate -- the depth-zero hoist stays with the early pass
// (depth-zero-hoist-dominant) and only the in-region class defers.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -mtt-tensix-optimize-priced-placement -fdump-tree-rvtt_invariant-details -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "park-ordering loop bb \\d+ .in-region 3. legacy=defer priced=keep DISAGREE .deciding=priced." "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "hoist deferred: residency-walk-ordering" 3 "rvtt_invariant" } }
// { dg-final { scan-tree-dump "depth-zero-hoist-dominant" "rvtt_invariant" } }

void park_priced_keep (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto d0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb8aa3b, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, d0, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
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
