// EL-vs-RESIDENCY ORDERING flag-off control: the fire twin's
// bodies with -mtt-tensix-optimize-park-ordering ABSENT (every other
// flag identical) keep the early invariant hoists byte-identically --
// the knob's only reachable effect is the named deferral.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "hoist deferred: residency-walk-ordering" "rvtt_invariant" } }

void ordering_defer_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_varied_walker (void)
{
  auto west = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned step = 0; step != 12; ++step)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (west, 0);
      __builtin_rvtt_sfppopc (0);
      auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb, 0, 0, 31);
      west = __builtin_rvtt_sfpmul (west, bias, 0);
    }
  __builtin_rvtt_sfpwritelreg (west, 2);
}
