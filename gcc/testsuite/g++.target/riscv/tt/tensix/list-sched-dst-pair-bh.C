// Check that the list scheduler does not hoist a Dst reload above a store to the same offset.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// The defless stores may print as either barrier class.
// { dg-final { scan-rtl-dump-times "List-schedule barrier: dst-access uid=\\d+" 4 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule barrier: (?:dst-access|scalar-or-defless) uid=\\d+" 4 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule deferred: repeated-row shape at uid=\\d+" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule: bb" "rvtt_schedule" } }

using vec_t = decltype (__builtin_rvtt_sfpreadlreg (0));

void dst_pair_row ()
{
  vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t p = __builtin_rvtt_sfpmad (x, x, x, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  __builtin_rvtt_sfpstore (nullptr, p, 64, 0, 0, 0, 7);
  /* Same-offset reload: RAW through Dst[64] with no LREG edge.  */
  vec_t r = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  vec_t s = __builtin_rvtt_sfpmad (r, x, r, 0);
  s = __builtin_rvtt_sfpmad (s, x, s, 0);
  s = __builtin_rvtt_sfpmad (s, x, s, 0);
  __builtin_rvtt_sfpstore (nullptr, s, 128, 0, 0, 0, 7);
}
