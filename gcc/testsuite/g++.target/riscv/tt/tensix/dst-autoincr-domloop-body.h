/* A runtime-counted loop (zero-trip capable: the loop guard precedes the
   preheader) with a conditional scalar arm inside the body.  Each row's Dst
   advance is absorbed and the slot program is placed once in the dedicated
   preheader: every instruction on every path from the program to a row --
   including both arms of the branch -- is proven unable to write the
   scratch slot's configuration.  DST_ARM expands to the conditional arm's
   statement; a foreign effect there must refuse the dominating placement
   on that path and fall back to per-group placement, never to
   unsoundness.

   Four rows per iteration: the final row's implicit advance crosses the
   loop backedge, and the iteration's own slot-occupying words cover the
   audited drained-frontend window with slots to spare (see
   rtl-rvtt-dst-autoincr.cc), so the dominating placement keeps firing.
   The skinny one-row rolled shape is the hardware-regressive witness class
   and lives in the rolled-tiny refusal twins instead.  */

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
