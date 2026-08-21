// Renamed-equivalent, varied-constant twin of raw-prera-fire10-bh.C
// (generality bar): different function and value names, odd input
// rows, a different output row, and the mirrored pair mix.  Fires
// identically -- admission is structural, never a name or a constant.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-pressure-schedule-prera -fdump-rtl-rvtt_lp_schedule_prera-details" }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule: bb \\d+ nodes=15 pressure 10 -> 7 makespan 15 -> 15 model-peak=7 candidate=model target=bh" "rvtt_lp_schedule_prera" } }

void twin_wide_ten (void)
{
  auto p = __builtin_rvtt_sfpload (nullptr, 33, 0, 0, 4, 7);
  auto q = __builtin_rvtt_sfpload (nullptr, 35, 0, 0, 4, 7);
  auto r = __builtin_rvtt_sfpload (nullptr, 37, 0, 0, 4, 7);
  auto s = __builtin_rvtt_sfpload (nullptr, 39, 0, 0, 4, 7);
  auto t = __builtin_rvtt_sfpload (nullptr, 41, 0, 0, 4, 7);
  auto w0 = __builtin_rvtt_sfpxor (t, s);
  auto w1 = __builtin_rvtt_sfpxor (s, r);
  auto w2 = __builtin_rvtt_sfpxor (r, q);
  auto w3 = __builtin_rvtt_sfpxor (q, p);
  auto w4 = __builtin_rvtt_sfpxor (p, t);
  auto w5 = __builtin_rvtt_sfpxor (t, r);
  auto w6 = __builtin_rvtt_sfpxor (s, q);
  auto w7 = __builtin_rvtt_sfpxor (r, p);
  auto x0 = __builtin_rvtt_sfpxor (w0, w1);
  auto x1 = __builtin_rvtt_sfpxor (w2, w3);
  auto x2 = __builtin_rvtt_sfpxor (w4, w5);
  auto x3 = __builtin_rvtt_sfpxor (w6, w7);
  auto y0 = __builtin_rvtt_sfpxor (x0, x1);
  auto y1 = __builtin_rvtt_sfpxor (x2, x3);
  auto z = __builtin_rvtt_sfpxor (y0, y1);
  __builtin_rvtt_sfpstore (nullptr, z, 250, 0, 0, 4, 7);
}
