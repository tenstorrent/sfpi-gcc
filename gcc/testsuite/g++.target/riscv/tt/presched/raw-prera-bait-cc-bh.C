// CC bait: CC-writing instructions (pushc/setcc/popc) bound the region
// by name, and the surrounding sub-region finds no joint improvement.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-pressure-schedule-prera -fdump-rtl-rvtt_lp_schedule_prera-details" }
// { dg-final { scan-rtl-dump-times "Prera-pressure-schedule barrier: cc-write uid=\\d+" 2 "rvtt_lp_schedule_prera" } }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule refused: no joint pressure/makespan improvement" "rvtt_lp_schedule_prera" } }
// { dg-final { scan-rtl-dump-not "Prera-pressure-schedule: bb" "rvtt_lp_schedule_prera" } }

void bait_cc (void)
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
  auto b = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 4, 7);
  auto c = __builtin_rvtt_sfpload (nullptr, 4, 0, 0, 4, 7);
  auto u0 = __builtin_rvtt_sfpxor (a, b);
  auto u1 = __builtin_rvtt_sfpxor (b, c);
  auto u2 = __builtin_rvtt_sfpxor (a, c);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (u0, 0);
  auto m = __builtin_rvtt_sfpxor (u1, u2);
  __builtin_rvtt_sfppopc (0);
  auto v = __builtin_rvtt_sfpxor (m, u0);
  __builtin_rvtt_sfpstore (nullptr, v, 192, 0, 0, 4, 7);
}
