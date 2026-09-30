// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 4 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "left in loop by LREG pressure" 3 "rvtt_invariant" } }

/* Near miss for partial selection: the seventh materialization has a real
   use after the loop.  It is not a hoist candidate even though the loop and
   constants otherwise have the same shape as the pressure test.  */
void independent_liveout_case ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  __typeof__ (x) liveout;
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3d100001, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c0, 0);
      auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3d200002, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c1, 0);
      auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3d300003, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c2, 0);
      auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x3d400004, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c3, 0);
      auto c4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3d500005, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c4, 0);
      auto c5 = __builtin_rvtt_sfpxloadi (nullptr, 0x3d600006, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c5, 0);
      liveout = __builtin_rvtt_sfpxloadi (nullptr, 0x3d700007, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, liveout, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpwritelreg (liveout, 1);
}
