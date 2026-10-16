// Check that the list scheduler treats SFPLUTFP32, whose latency is unaudited, as a barrier.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// SFPLUTFP32 is refused at the effect check, before the latency check.
// { dg-final { scan-rtl-dump-times "List-schedule barrier: (?:effect-opaque|unaudited-latency) uid=\\d+" 1 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule refused: no modeled makespan decrease" 1 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule: bb" "rvtt_schedule" } }

void lutfp32_row ()
{
  auto t0 = __builtin_rvtt_sfpreadlreg (0);
  auto t1 = __builtin_rvtt_sfpreadlreg (1);
  auto t2 = __builtin_rvtt_sfpreadlreg (2);
  auto v  = __builtin_rvtt_sfpreadlreg (3);
  auto p  = __builtin_rvtt_sfpreadlreg (4);
  auto x  = __builtin_rvtt_sfpreadlreg (5);
  /* Serial mad chain.  */
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  /* Chain-independent LUT instruction with unaudited latency.  */
  auto r = __builtin_rvtt_sfplutfp32_3r (t0, t1, t2, v, 10);
  __builtin_rvtt_sfpwritelreg (p, 4);
  __builtin_rvtt_sfpwritelreg (r, 3);
}
