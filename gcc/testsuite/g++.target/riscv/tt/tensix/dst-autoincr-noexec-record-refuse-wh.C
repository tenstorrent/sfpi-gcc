// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Wormhole twin of the in-nest no-exec-record composition refusal: the
// guard is capability-table generic (the mod-write retirement is
// unaudited on both frontends; the hardware refutation is the BH
// 2x2 and Wormhole adopts the same-frontend-class conservative verdict).
// Dump-only scans: default Wormhole replay formation reshapes delivery.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: mod-write-noexec-record-composition-unaudited .no-exec replay capture within the drained-frontend window of the group.s stores, bb \[0-9\]+, capture bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }

using vec_t = __xtt_vector;

void
noexec_record_inside_nest ()
{
  for (unsigned face = 0; face != 4; ++face)
    {
      __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 1);
      auto x = __builtin_rvtt_sfpreadlreg (0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_sfpwritelreg (x, 0);

      for (unsigned ix = 0; ix != 8; ++ix)
	{
	  vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 3);
	  vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
	  p = __builtin_rvtt_sfpmul (p, a, 0);
	  p = __builtin_rvtt_sfpmul (p, a, 0);
	  p = __builtin_rvtt_sfpmul (p, a, 0);
	  p = __builtin_rvtt_sfpmul (p, a, 0);
	  p = __builtin_rvtt_sfpmul (p, a, 0);
	  __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 3);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}
