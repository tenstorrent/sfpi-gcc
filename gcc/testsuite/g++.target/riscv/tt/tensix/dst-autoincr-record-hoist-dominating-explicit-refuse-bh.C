// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_dst_autoincr-details" }
// { dg-final { check-function-bodies "**" "" } }
//
// With record hoisting enabled, a no-exec capture that dominates an explicit
// mod-write group is conservatively quarantined on Blackhole.  The capture is
// not the group's deliverer, and this function's CFG cannot prove that a prior
// invocation's explicit mod-write has retired before the next invocation
// enters the capture.  Retain the explicit TTINCRWC fallback; this is not a
// capture-count or frontend-distance threshold.
// { dg-final { scan-rtl-dump "record-hoist-enabled no-exec capture dominates explicit mod-write group" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group: bb" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTSETC16\t34, 2" } }

using vec_t = __xtt_vector;

void
dominating_capture_then_explicit_rows ()
{
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);

  for (unsigned face = 0; face != 4; ++face)
    for (unsigned ix = 0; ix != 8; ++ix)
      {
	vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
	vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
	p = __builtin_rvtt_sfpmul (p, a, 0);
	p = __builtin_rvtt_sfpmul (p, a, 0);
	p = __builtin_rvtt_sfpmul (p, a, 0);
	p = __builtin_rvtt_sfpmul (p, a, 0);
	p = __builtin_rvtt_sfpmul (p, a, 0);
	__builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
	__builtin_rvtt_ttincrwc (0, 2, 0, 0);
      }
}

/*
**_Z37dominating_capture_then_explicit_rowsv:
**...
**	TTINCRWC	0, 2, 0, 0
**...
**	ret
*/
