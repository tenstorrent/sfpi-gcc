// An unrepresented call in the face-loop body can own architectural LREG
// state: the face-level hoist refuses; the inner loop still hoists.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "function has opaque LREG state" 1 "rvtt_invariant" } }

extern void external_stage_sync ();

void face_call_body ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned face = 0; face != 4; ++face)
    {
      for (unsigned ix = 0; ix != 8; ++ix)
	{
	  auto a = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
	  auto b = __builtin_rvtt_sfpxloadi (nullptr, 0xbf91c2e7, 0, 0, 31);
	  x = __builtin_rvtt_sfpmad (x, a, b, 0);
	}
      external_stage_sync ();
      __builtin_rvtt_ttdstface ();
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
