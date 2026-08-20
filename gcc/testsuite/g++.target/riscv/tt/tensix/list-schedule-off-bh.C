// Default-off: without -mtt-tensix-optimize-list-schedule the list
// scheduler never runs -- no dump line, no reorder, the serial
// dual-Horner stream survives byte-identically (the existing bubble-fill
// phases are separately flagged and also off here).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-not "List-schedule" "rvtt_schedule" } }

void dual_horner_rational_off ()
{
  auto x  = __builtin_rvtt_sfpreadlreg (0);
  auto p  = __builtin_rvtt_sfpreadlreg (1);
  auto q  = __builtin_rvtt_sfpreadlreg (2);
  auto c1 = __builtin_rvtt_sfpreadlreg (3);
  auto c2 = __builtin_rvtt_sfpreadlreg (4);
  auto c3 = __builtin_rvtt_sfpreadlreg (5);
  p = __builtin_rvtt_sfpmad (p, x, c1, 0);
  p = __builtin_rvtt_sfpmad (p, x, c2, 0);
  p = __builtin_rvtt_sfpmad (p, x, c3, 0);
  q = __builtin_rvtt_sfpmad (q, x, c1, 0);
  q = __builtin_rvtt_sfpmad (q, x, c2, 0);
  q = __builtin_rvtt_sfpmad (q, x, c3, 0);
  __builtin_rvtt_sfpwritelreg (p, 1);
  __builtin_rvtt_sfpwritelreg (q, 2);
}
