/* Runtime-counted four-row loop with a conditional scalar arm (DST_ARM) in
   the body.  */

using vec_t = __xtt_vector;

unsigned
DOMLOOP_FN (unsigned n, unsigned sel)
{
  unsigned acc = 0;
  for (unsigned ix = 0; ix != n; ++ix)
    {
      vec_t a = __builtin_rvtt_sfpload (nullptr, DST_ADDR, 0, 0, 0,
					DST_MODE);
      vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
      __builtin_rvtt_sfpstore (nullptr, p, DST_ADDR, 0, 0, 0, DST_MODE);
      __builtin_rvtt_ttincrwc (0, DST_STRIDE, 0, 0);
      vec_t b = __builtin_rvtt_sfpload (nullptr, DST_ADDR, 0, 0, 0,
					DST_MODE);
      vec_t q = __builtin_rvtt_sfpmul (b, b, 0);
      __builtin_rvtt_sfpstore (nullptr, q, DST_ADDR, 0, 0, 0, DST_MODE);
      __builtin_rvtt_ttincrwc (0, DST_STRIDE, 0, 0);
      vec_t c = __builtin_rvtt_sfpload (nullptr, DST_ADDR, 0, 0, 0,
					DST_MODE);
      vec_t r = __builtin_rvtt_sfpmul (c, c, 0);
      __builtin_rvtt_sfpstore (nullptr, r, DST_ADDR, 0, 0, 0, DST_MODE);
      __builtin_rvtt_ttincrwc (0, DST_STRIDE, 0, 0);
      vec_t d = __builtin_rvtt_sfpload (nullptr, DST_ADDR, 0, 0, 0,
					DST_MODE);
      vec_t s = __builtin_rvtt_sfpmul (d, d, 0);
      __builtin_rvtt_sfpstore (nullptr, s, DST_ADDR, 0, 0, 0, DST_MODE);
      __builtin_rvtt_ttincrwc (0, DST_STRIDE, 0, 0);
      if (sel & (1u << ix))
	{
	  DST_ARM;
	}
    }
  return acc;
}
