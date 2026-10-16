// Check that the pre-RA loop scheduler moves an add into the mad shadow of a self-loop block.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-pressure-schedule-prera-loop -fdump-rtl-rvtt_lp_schedule_prera-details" }
// { dg-final { scan-rtl-dump-not "deferred: cyclic row adjacency" "rvtt_lp_schedule_prera" } }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule capped candidate: peak=3 end=6 moves=1" "rvtt_lp_schedule_prera" } }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule: bb \\d+ nodes=4 pressure 3 -> 3 makespan 7 -> 6 model-peak=3 candidate=capped target=bh" "rvtt_lp_schedule_prera" } }

void loop_fill (void)
{
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 3, 7);
      auto b = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 3, 7);
      auto u = __builtin_rvtt_sfpmul (a, b, 0);
      auto v = __builtin_rvtt_sfpmul (u, u, 0);
      auto w = __builtin_rvtt_sfpadd (a, b, 0);
      auto z = __builtin_rvtt_sfpmad (v, w, w, 0);
      __builtin_rvtt_sfpstore (nullptr, z, 192, 0, 0, 3, 7);
    }
}
