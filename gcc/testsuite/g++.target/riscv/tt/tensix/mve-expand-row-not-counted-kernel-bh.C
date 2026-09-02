// MVE realization adjudication outside the counted-kernel seam (item
// #5 stage 2): with the realization flag live, a REGION placement that
// owes a FITTING expansion (kmin 2, demand within the file) on a row
// the counted-kernel seam does not admit (a barrier-chopped CC-atom
// row -- the cross-row pairing's vocabulary refuses its predicated
// words) is adjudicated by name instead of dangling as an owed fact:
// the expansion cannot be performed without breaking replay formation.
// The region whose demand does NOT fit keeps stage 1's own name (both
// scanned).  Flat orders still compete exactly as in stage 1.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-ims -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-mve-expand -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "List-schedule \\(ims\\) refused: mve-expand-row-not-counted-kernel at uid=\\d+ in bb \\d+ \\(kmin=2 demand=\\d+ invariants=\\d+ fits; flat order still offered\\)" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "List-schedule \\(ims\\) refused: mve-rename-exhausted at uid=\\d+ in bb \\d+ \\(kmin=\\d+ demand=\\d+ capacity=\\d+ invariants=\\d+; flat order still offered\\)" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "MVE owed" "rvtt_schedule" } }

void mve_region_owed ()
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
