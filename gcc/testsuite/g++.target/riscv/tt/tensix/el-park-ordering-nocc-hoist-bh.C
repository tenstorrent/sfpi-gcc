// EL-vs-RESIDENCY ORDERING near-miss pair (lane HN), knob ON with both
// late flags: (1) a loop WITHOUT CC machinery keeps the early
// invariant hoist byte-identically -- the deferral targets exactly the
// CC-restore loop class the late peel/park walk owns; (2) a CC-restore
// loop with NO invariant immediate has nothing to defer and prints no
// ordering line.  Two unrelated shapes proving the deferral's scope.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "hoist deferred: residency-walk-ordering" "rvtt_invariant" } }

void plain_loop_keeps_hoist (void)
{
  auto acc = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 16; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3f2a7c11, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, gain, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 0);
}

void cc_loop_without_candidates (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (1);
  auto y = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
      x = __builtin_rvtt_sfpmul (x, y, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 1);
}
