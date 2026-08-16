/* Post-migration reduced kernel shape: an outer face loop whose latch
   carries the TYPED face advance (an RVTT call, so the face-loop region is
   non-opaque), and an inner constant-trip loop with invariant loadi.  The
   loads hoist stepwise: out of the inner loop into the face body, then out
   of the face loop entirely.  */
void FACE_FN ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned face = 0; face != FACE_TRIPS; ++face)
    {
      for (unsigned ix = 0; ix != FACE_ROWS; ++ix)
	{
	  auto a = __builtin_rvtt_sfpxloadi (nullptr, FACE_K0, 0, 0, 31);
	  auto b = __builtin_rvtt_sfpxloadi (nullptr, FACE_K1, 0, 0, 31);
	  x = __builtin_rvtt_sfpmad (x, a, b, 0);
	}
      FACE_ADVANCE;
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
