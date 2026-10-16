// Check that register pressure in the face body keeps hoisted loads at the inner preheader.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "left in loop by LREG pressure" 2 "rvtt_invariant" } }

void face_pressure ()
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
      auto y0 = __builtin_rvtt_sfpreadlreg (1);
      auto y1 = __builtin_rvtt_sfpreadlreg (2);
      auto y2 = __builtin_rvtt_sfpreadlreg (3);
      auto y3 = __builtin_rvtt_sfpreadlreg (4);
      auto y4 = __builtin_rvtt_sfpreadlreg (5);
      auto y5 = __builtin_rvtt_sfpreadlreg (6);
      auto y6 = __builtin_rvtt_sfpreadlreg (7);
      x = __builtin_rvtt_sfpmad (x, y0, y1, 0);
      x = __builtin_rvtt_sfpmad (x, y2, y3, 0);
      x = __builtin_rvtt_sfpmad (x, y4, y5, 0);
      x = __builtin_rvtt_sfpmul (x, y6, 0);
      __builtin_rvtt_ttdstface ();
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
