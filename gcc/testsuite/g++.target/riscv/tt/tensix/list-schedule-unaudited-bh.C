// Unaudited result latency refuses by name: SFPTRANSP8 carries audited
// typed effects but no audited result-latency entry (the transpose
// class is outside the single-latency vocabulary), so it never
// schedules -- it is a named barrier that bounds the region, and the
// single-chain sub-regions it creates find no modeled win and stay
// byte-identical.  Never guessed: the barrier name is the audit gap,
// not a heuristic.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "List-schedule barrier: unaudited-latency uid=\\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule: bb" "rvtt_schedule" } }

void unaudited_latency_bounds_region ()
{
  auto v0 = __builtin_rvtt_sfpreadlreg (0);
  auto v1 = __builtin_rvtt_sfpreadlreg (1);
  auto v2 = __builtin_rvtt_sfpreadlreg (2);
  auto v3 = __builtin_rvtt_sfpreadlreg (3);
  auto i0 = __builtin_rvtt_sfpreadlreg (4);
  auto i1 = __builtin_rvtt_sfpreadlreg (5);
  auto i2 = __builtin_rvtt_sfpreadlreg (6);
  auto i3 = __builtin_rvtt_sfpreadlreg (7);
  auto a1 = __builtin_rvtt_sfpmul (v0, v0, 0);
  auto a2 = __builtin_rvtt_sfpmul (a1, a1, 0);
  auto a3 = __builtin_rvtt_sfpmul (a2, a2, 0);
  auto t = __builtin_rvtt_sfptransp8 (a3, v1, v2, v3, i0, i1, i2, i3);
  auto x0 = __builtin_rvtt_sfpselect4 (t, 0);
  auto b1 = __builtin_rvtt_sfpadd (x0, x0, 0);
  auto b2 = __builtin_rvtt_sfpadd (b1, b1, 0);
  auto b3 = __builtin_rvtt_sfpadd (b2, b2, 0);
  __builtin_rvtt_sfpwritelreg (b3, 0);
}
