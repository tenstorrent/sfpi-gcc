// IMS RENAMED-EQUIVALENT / VARIED-SHAPE generality twin (the modulo-scheduling tier; the
// GY adversary recipe): different function and value names, a
// different trip count (24), the two regions in REVERSED order (the
// mul/mad mixed span first, the serial-chain-plus-pair span second),
// varied CC compare mods, and DISJOINT Dst rows (load at address 4,
// store at address 8).  The IMS candidate generation must key on
// structural facts alone -- dependence-distance graph, MII, modulo
// placement, strict whole-row acceptance -- and still commit both
// interior regions.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-ims -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "List-schedule \\(ims\\) row: bb \\d+ words=\\d+ ResMII=\\d+ RecMII=\\d+ row-II=\\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule \\(ims-interior\\): bb \\d+ region at uid=\\d+ nodes=\\d+ row II \\d+ -> \\d+ target=bh" 2 "rvtt_schedule" } }

void qzkd_rows_rev ()
{
  for (int lap = 0; lap < 24; ++lap)
    {
      auto g = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (g, 2);
      auto a1 = __builtin_rvtt_sfpmul (g, g, 0);
      auto a2 = __builtin_rvtt_sfpmul (a1, a1, 0);
      auto b1 = __builtin_rvtt_sfpmad (g, g, g, 0);
      auto b2 = __builtin_rvtt_sfpmad (b1, g, g, 0);
      auto y  = __builtin_rvtt_sfpadd (a2, b2, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (y, 0);
      auto t1 = __builtin_rvtt_sfpmul (y, y, 0);
      auto t2 = __builtin_rvtt_sfpmul (t1, t1, 0);
      auto t3 = __builtin_rvtt_sfpmul (t2, t2, 0);
      auto u1 = __builtin_rvtt_sfpmad (y, y, y, 0);
      auto u2 = __builtin_rvtt_sfpmad (u1, y, y, 0);
      auto z  = __builtin_rvtt_sfpmad (t3, u1, u2, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, z, 0, 0, 0, 8, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
