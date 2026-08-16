// A zero-trip-capable face loop (runtime count): the face-level hoist
// would speculate the architectural LREG writes, so only the inner loop
// hoists; the loads re-execute per face.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "function has opaque LREG state" "rvtt_invariant" } }

void face_zero_trip (unsigned nfaces)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned face = 0; face != nfaces; ++face)
    {
      for (unsigned ix = 0; ix != 8; ++ix)
	{
	  auto a = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
	  auto b = __builtin_rvtt_sfpxloadi (nullptr, 0xbf91c2e7, 0, 0, 31);
	  x = __builtin_rvtt_sfpmad (x, a, b, 0);
	}
      __builtin_rvtt_ttdstface ();
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
