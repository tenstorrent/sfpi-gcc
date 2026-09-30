/* A completely unrolled counted row loop whose final copy is register-
   allocated differently: the invariant constants die at the last row, so
   its temporaries take their registers.  Replay formation captures the
   identical leading rows; the launch-conversion proof must recognize the
   renamed final row as effect-isomorphic to the payload under a value map
   and execute it as one more launch instead of inline expansion.  */

using vec_t = __xtt_vector;

void
CONVERT_FN ()
{
  for (unsigned ix = 0; ix != CONVERT_TRIPS; ++ix)
    {
      vec_t a = __builtin_rvtt_sfpload (nullptr, CONVERT_ADDR, 0, 0, 0,
					CONVERT_MODE);
      vec_t c0 = __builtin_rvtt_sfpxloadi (nullptr, CONVERT_K0, 0, 0, 31);
      vec_t c1 = __builtin_rvtt_sfpxloadi (nullptr, CONVERT_K1, 0, 0, 31);
      vec_t c2 = __builtin_rvtt_sfpxloadi (nullptr, CONVERT_K2, 0, 0, 31);
      vec_t c3 = __builtin_rvtt_sfpxloadi (nullptr, CONVERT_K3, 0, 0, 31);
      vec_t t1 = __builtin_rvtt_sfpmul (a, c0, 0);
      vec_t t2 = __builtin_rvtt_sfpmul (t1, c1, 0);
      vec_t t3 = __builtin_rvtt_sfpmul (t2, c2, 0);
      vec_t t4 = __builtin_rvtt_sfpmul (t3, t1, 0);
      vec_t t5 = __builtin_rvtt_sfpmul (t4, c3, 0);
      __builtin_rvtt_sfpstore (nullptr, t5, CONVERT_ADDR, 0, 0, 0,
			       CONVERT_MODE);
      __builtin_rvtt_ttincrwc (0, CONVERT_STRIDE, 0, 0);
    }
}
