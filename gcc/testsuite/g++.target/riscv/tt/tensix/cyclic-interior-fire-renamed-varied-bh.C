// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-cyclic-region-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// Cyclic-interior RENAMED-EQUIVALENT / VARIED-SHAPE adversary twin
// (renamed-twin recipe): different function and value names, a
// different trip count (24) and a LONGER serial latency chain (FOUR
// muls) with the independent mad pair still available to fill its
// shadows.  The re-list-scheduling must key on the structural
// facts (an interior region between CC/Dst barrier words whose serial
// chain leaves shadows an independent pair can fill, accepted only on
// a strict whole-row cyclic-II decrease) and still commit a reorder.
// { dg-final { scan-rtl-dump "List-schedule \\(cyclic-interior\\): bb \\d+ region at uid=\\d+ nodes=\\d+ row II \\d+ -> \\d+ target=bh" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "List-schedule slot-order=0 uid=\\d+" "rvtt_schedule" } }

void audit_ip_interior_row ()
{
  for (int lap = 0; lap < 24; ++lap)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (v, 0);
      auto c1 = __builtin_rvtt_sfpmul (v, v, 0);
      auto c2 = __builtin_rvtt_sfpmul (c1, c1, 0);
      auto c3 = __builtin_rvtt_sfpmul (c2, c2, 0);
      auto c4 = __builtin_rvtt_sfpmul (c3, c3, 0);
      auto p1 = __builtin_rvtt_sfpmad (v, v, v, 0);
      auto p2 = __builtin_rvtt_sfpmad (p1, v, v, 0);
      auto w  = __builtin_rvtt_sfpmad (c4, p1, p2, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, w, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
