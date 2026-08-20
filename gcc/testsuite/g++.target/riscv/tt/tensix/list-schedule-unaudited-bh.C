// Unaudited result latency refuses by name: SFPSHFT2 is deliberately
// outside the single-latency vocabulary (mod-dependent next-cycle
// register constraints, SFPSHFT2.md), so it never schedules -- it is a
// named barrier that bounds the region, and the two single-chain
// sub-regions it creates find no modeled win and stay byte-identical.
// Never guessed: the barrier name is the audit gap, not a heuristic.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-times "List-schedule barrier: unaudited-latency uid=\\d+" 1 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule: bb" "rvtt_schedule" } }

void unaudited_latency_bounds_region ()
{
  auto u = __builtin_rvtt_sfpreadlreg (0);
  auto v = __builtin_rvtt_sfpreadlreg (1);
  auto a1 = __builtin_rvtt_sfpmul (u, u, 0);
  auto a2 = __builtin_rvtt_sfpmul (a1, a1, 0);
  auto a3 = __builtin_rvtt_sfpmul (a2, a2, 0);
  auto s = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3);
  auto b1 = __builtin_rvtt_sfpadd (s, s, 0);
  auto b2 = __builtin_rvtt_sfpadd (b1, b1, 0);
  auto b3 = __builtin_rvtt_sfpadd (b2, b2, 0);
  __builtin_rvtt_sfpwritelreg (a3, 0);
  __builtin_rvtt_sfpwritelreg (b3, 1);
}
