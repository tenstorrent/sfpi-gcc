// Default-off: without -mtt-tensix-optimize-interlock-schedule the
// fire shape from interlock-fill-bh.C is untouched.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-not "Interlock-fill moved" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Interlock fill refused" "rvtt_schedule" } }

void interlock_fill_deep ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  auto p = __builtin_rvtt_sfpmul (a, a, 0);
  auto q = __builtin_rvtt_sfpmul (p, p, 0);
  auto f = __builtin_rvtt_sfpand (c, c);
  auto g = __builtin_rvtt_sfpor (d, d);
  __builtin_rvtt_sfpwritelreg (q, 0);
  __builtin_rvtt_sfpwritelreg (f, 2);
  __builtin_rvtt_sfpwritelreg (g, 3);
}
