// IMS BUDGET-EXHAUSTION twin (item #5): the fire kernel under an
// absolute placement budget of ONE (-mtt-tensix-ims-budget=1, the
// testing knob) -- Rau's eviction loop runs out at every II below the
// acceptance bound in every region, refuses ims-budget-exhausted by
// name, and nothing commits: the original order is kept.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-ims -mtt-tensix-ims-budget=1 -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-times "ims-budget-exhausted at uid=\\d+ in bb \\d+ \\(MII \\d+, bound \\d+, budget 1\\)" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "\\(ims-interior\\)" "rvtt_schedule" } }

void kd_ims_budget ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (v, 0);
      auto t1 = __builtin_rvtt_sfpmul (v, v, 0);
      auto t2 = __builtin_rvtt_sfpmul (t1, t1, 0);
      auto t3 = __builtin_rvtt_sfpmul (t2, t2, 0);
      auto u1 = __builtin_rvtt_sfpmad (v, v, v, 0);
      auto u2 = __builtin_rvtt_sfpmad (u1, v, v, 0);
      auto w  = __builtin_rvtt_sfpmad (t3, u1, u2, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (w, 2);
      auto a1 = __builtin_rvtt_sfpmul (w, w, 0);
      auto a2 = __builtin_rvtt_sfpmul (a1, a1, 0);
      auto b1 = __builtin_rvtt_sfpmad (w, w, w, 0);
      auto b2 = __builtin_rvtt_sfpmad (b1, w, w, 0);
      auto z  = __builtin_rvtt_sfpadd (a2, b2, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, z, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
