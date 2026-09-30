// Pressure-equal makespan fire: the ECC rank slots the independent add
// into the mad-family RAW shadow without raising liveness -- the joint
// acceptance admits a strict makespan decrease at unchanged pressure.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-pressure-schedule-prera -fdump-rtl-rvtt_lp_schedule_prera-details" }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule: bb \\d+ nodes=4 pressure 3 -> 3 makespan 7 -> 6 model-peak=3 candidate=ecc target=bh" "rvtt_lp_schedule_prera" } }

void makespan_fire (void)
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 3, 7);
  auto b = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 3, 7);
  auto u = __builtin_rvtt_sfpmul (a, b, 0);
  auto v = __builtin_rvtt_sfpmul (u, u, 0);
  auto w = __builtin_rvtt_sfpadd (a, b, 0);
  auto z = __builtin_rvtt_sfpmad (v, w, w, 0);
  __builtin_rvtt_sfpstore (nullptr, z, 192, 0, 0, 3, 7);
}
