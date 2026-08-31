// IMS FIRE (item #5): a TWO-REGION self-loop row -- a Dst row loop
// chopped by CC and Dst barrier words into two interior regions with
// different insn-code signatures.  Under -mtt-tensix-optimize-ims ALONE
// (no legacy cyclic flag) each region gets a Rau iterative-modulo-
// scheduling candidate: MII = max(ResMII, RecMII-exact) from the
// engine's one marshalled dependence-distance graph, placement against
// the single-issue modulo reservation table, order = placement slots
// ascending -- and commits ONLY on the established strict whole-row
// steady-state II decrease.  Both regions commit here.  The row-level
// ResMII/RecMII line is the exact-tier floor artifact.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-ims -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "List-schedule \\(ims\\) row: bb \\d+ words=\\d+ ResMII=\\d+ RecMII=\\d+ row-II=\\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "List-schedule \\(ims\\) region: bb \\d+ nodes=6 ResMII=\\d+ RecMII=\\d+ MII=\\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule \\(ims-interior\\): bb \\d+ region at uid=\\d+ nodes=\\d+ row II \\d+ -> \\d+ target=bh" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "List-schedule slot-order=0 uid=\\d+" "rvtt_schedule" } }

void kd_ims_fire ()
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
