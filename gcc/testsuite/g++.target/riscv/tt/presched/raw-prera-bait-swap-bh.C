// SFPSWAP bait: the architectural next-slot acceptance stall is
// outside the audited single-latency vocabulary, so the swap is a
// named barrier (pressure-model-unaudited-producer) and the tiny
// sub-regions it leaves find no joint improvement -- byte-identical.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-pressure-schedule-prera -fdump-rtl-rvtt_lp_schedule_prera-details" }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule barrier: pressure-model-unaudited-producer uid=\\d+" "rvtt_lp_schedule_prera" } }
// { dg-final { scan-rtl-dump-not "Prera-pressure-schedule: bb" "rvtt_lp_schedule_prera" } }

void bait_swap (void)
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
  auto b = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 4, 7);
  auto c = __builtin_rvtt_sfpload (nullptr, 4, 0, 0, 4, 7);
  auto u0 = __builtin_rvtt_sfpxor (a, b);
  auto u1 = __builtin_rvtt_sfpxor (b, c);
  auto u2 = __builtin_rvtt_sfpxor (a, c);
  auto pair = __builtin_rvtt_sfpswap (u0, u1, 1);
  auto lo = __builtin_rvtt_sfpselect2 (pair, 0);
  auto v0 = __builtin_rvtt_sfpxor (lo, u2);
  auto v1 = __builtin_rvtt_sfpxor (v0, a);
  auto v2 = __builtin_rvtt_sfpxor (v1, c);
  __builtin_rvtt_sfpstore (nullptr, v2, 192, 0, 0, 4, 7);
}
