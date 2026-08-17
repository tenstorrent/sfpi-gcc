// Near misses for the interlock-stall shadow fill.
// 1. A producer without an audited result latency (SFPGT carries no
//    entry) refuses by name even with a filler available.
// 2. A move whose vacated seam exposes an equal stall (the consumer's
//    own dependent successor becomes adjacent) refuses on the modeled
//    stall accounting: no net decrease, byte-identical code.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-interlock-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "Interlock fill refused: unaudited result latency for producer uid=\\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Interlock fill refused: no modeled stall decrease moving uid=\\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Interlock-fill moved" "rvtt_schedule" } }

void unaudited_producer_refuses ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  // SFPGT with SET_VD writes its destination; its latency is unaudited.
  auto p = __builtin_rvtt_sfpgt (a, b, 8);
  auto q = __builtin_rvtt_sfpand (p, p);
  auto f = __builtin_rvtt_sfpor (c, c);
  __builtin_rvtt_sfpwritelreg (q, 0);
  __builtin_rvtt_sfpwritelreg (f, 2);
}

void no_stall_decrease_refuses ()
{
  auto a = __builtin_rvtt_sfpreadlreg (4);
  auto c = __builtin_rvtt_sfpreadlreg (6);
  auto p = __builtin_rvtt_sfpmul (a, a, 0);
  auto q = __builtin_rvtt_sfpmul (p, p, 0);
  auto f = __builtin_rvtt_sfpand (c, c);
  auto h = __builtin_rvtt_sfpxor (q, q);
  // Moving f between p and q would close the p->q stall but make q
  // adjacent to its own dependent reader h: net modeled change is
  // zero, so the block stays byte-identical.
  __builtin_rvtt_sfpwritelreg (f, 6);
  __builtin_rvtt_sfpwritelreg (h, 4);
}
