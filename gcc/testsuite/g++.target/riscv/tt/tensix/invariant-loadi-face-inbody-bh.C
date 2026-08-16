// The typed face advance inside a single counted loop body is a pure
// Dst/RWC counter effect: it neither changes an invariant SFPLOADI value
// nor the incoming CC state, so it is not a hoist barrier (an opaque raw
// word in the same position refuses -- see the scoped tests).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "function has opaque LREG state" "rvtt_invariant" } }

void face_advance_in_body ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      auto a = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      auto b = __builtin_rvtt_sfpxloadi (nullptr, 0xbf91c2e7, 0, 0, 31);
      x = __builtin_rvtt_sfpmad (x, a, b, 0);
      __builtin_rvtt_ttdstface ();
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
