// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Renamed-equivalent, varied-constants twin of the in-nest no-exec-record
// composition refusal: different names, capture length/begin/slot base,
// row body, stride, and trip counts -- the refusal keys on the no-exec
// capture's placement, not on any shape fingerprint.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: mod-write-noexec-record-composition-unaudited .no-exec replay capture within the drained-frontend window of the group.s stores, bb \[0-9\]+, capture bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }
// { dg-final { scan-assembler-not "TTSETC16\t34" } }

using wide_lane = __xtt_vector;

void
different_window_shape ()
{
  for (unsigned blk = 0; blk != 6; ++blk)
    {
      __builtin_rvtt_ttreplay (nullptr, 5, 0, 0, 8, 0, 1);
      auto v = __builtin_rvtt_sfpreadlreg (2);
      v = __builtin_rvtt_sfpmul (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpmul (v, v, 0);
      v = __builtin_rvtt_sfpadd (v, v, 0);
      v = __builtin_rvtt_sfpmul (v, v, 0);
      __builtin_rvtt_sfpwritelreg (v, 2);

      for (unsigned rep = 0; rep != 24; ++rep)
	{
	  wide_lane a = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
	  wide_lane q = __builtin_rvtt_sfpadd (a, a, 0);
	  q = __builtin_rvtt_sfpmul (q, a, 0);
	  q = __builtin_rvtt_sfpadd (q, a, 0);
	  q = __builtin_rvtt_sfpmul (q, a, 0);
	  q = __builtin_rvtt_sfpadd (q, a, 0);
	  q = __builtin_rvtt_sfpmul (q, a, 0);
	  __builtin_rvtt_sfpstore (nullptr, q, 64, 0, 0, 0, 7);
	  __builtin_rvtt_ttincrwc (0, 4, 0, 0);
	}
    }
}
