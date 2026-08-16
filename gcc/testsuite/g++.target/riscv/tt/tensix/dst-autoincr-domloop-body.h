/* A runtime-counted loop (zero-trip capable: the loop guard precedes the
   preheader) with a conditional scalar arm inside the body.  The row's Dst
   advance is absorbed and the slot program is placed once in the dedicated
   preheader: every instruction on every path from the program to a row --
   including both arms of the branch -- is proven unable to write the
   scratch slot's configuration.  DST_ARM expands to the conditional arm's
   statement; a foreign effect there must refuse the dominating placement
   on that path and fall back to per-group placement, never to
   unsoundness.  */

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
      if (sel & (1u << ix))
	{
	  DST_ARM;
	}
    }
  return acc;
}
