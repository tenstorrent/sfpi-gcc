// Cyclic-interior FIRE on Wormhole: the same barrier-chopped self-loop
// mechanism under the WH delay discipline -- the commit re-verifies the
// nop inserter's pad-site probe (the WH correctness carrier) and the
// entry producer's pad state before keeping the reorder.  A CC-atom
// row (pushc/setcc ... popc) whose interior region carries a serial
// mul chain plus an independent mad pair: the region re-list-schedules
// and commits on a strict whole-row modeled-II decrease.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-cyclic-region-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "List-schedule \\(cyclic-interior\\): bb \\d+ region at uid=\\d+ nodes=7 row II \\d+ -> \\d+ target=wh" "rvtt_schedule" } }

void cis_fire_wh ()
{
  auto x   = __builtin_rvtt_sfpreadlreg (0);
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned r = 0; r != 32; ++r)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      auto t1 = __builtin_rvtt_sfpmul (x, x, 0);
      auto t2 = __builtin_rvtt_sfpmul (t1, t1, 0);
      auto t3 = __builtin_rvtt_sfpmul (t2, t2, 0);
      auto u1 = __builtin_rvtt_sfpmad (x, x, x, 0);
      auto u2 = __builtin_rvtt_sfpmad (u1, x, x, 0);
      acc = __builtin_rvtt_sfpand (acc, __builtin_rvtt_sfpmad (t3, u1, u2, 0));
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
