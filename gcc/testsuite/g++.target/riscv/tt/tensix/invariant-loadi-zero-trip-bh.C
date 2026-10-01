// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }

void zero_trip (unsigned count)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != count; ++ix)
    {
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
