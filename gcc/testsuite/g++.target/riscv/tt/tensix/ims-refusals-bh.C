// IMS REFUSALS (the modulo-scheduling tier), each by name, original order kept:
//  - kd_ims_noimprove: the interior region is a pure serial chain; the
//    IMS candidate cannot prove a strict whole-row II decrease ->
//    ims-no-ii-decrease.
//  - kd_ims_swap_row: an SFPSWAP in the row -- a result-bearing word
//    whose architectural next-slot acceptance stall keeps it outside
//    the audited single-latency vocabulary (audited_latency -1, the
//    lane-BM contract) -> ims-unaudited-latency; IMS refuses the WHOLE
//    row rather than place under a floored fact (no (ims-interior)
//    commit anywhere in that row).
//  - kd_ims_mve: the fire shape's placement wants cross-iteration
//    overlap (value lifetimes beyond the II, kmin 2) and the steady-
//    state live-copy demand exceeds the 8-LREG file -> the EXPANSION
//    refuses mve-rename-exhausted while the flat order still commits
//    under the strict acceptance (both lines scanned).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-ims -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "ims-no-ii-decrease at uid=\\d+ in bb \\d+ \\(\\d+ -> \\d+\\)" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "ims-unaudited-latency uid=\\d+ in bb \\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "mve-rename-exhausted at uid=\\d+ in bb \\d+ \\(kmin=\\d+ demand=\\d+ capacity=\\d+ invariants=\\d+; flat order still offered\\)" "rvtt_schedule" } }

void kd_ims_noimprove ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (v, 0);
      auto t1 = __builtin_rvtt_sfpmul (v, v, 0);
      auto t2 = __builtin_rvtt_sfpmul (t1, t1, 0);
      auto t3 = __builtin_rvtt_sfpmul (t2, t2, 0);
      auto t4 = __builtin_rvtt_sfpmad (t3, t3, v, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, t4, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void kd_ims_swap_row ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v  = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (v, 0);
      auto t1 = __builtin_rvtt_sfpmul (v, v, 0);
      auto t2 = __builtin_rvtt_sfpmul (t1, t1, 0);
      auto t3 = __builtin_rvtt_sfpmul (t2, t2, 0);
      auto u1 = __builtin_rvtt_sfpmad (v, v, v, 0);
      auto u2 = __builtin_rvtt_sfpmad (u1, v, v, 0);
      auto w  = __builtin_rvtt_sfpmad (t3, u1, u2, 0);
      __builtin_rvtt_sfppopc (0);
      auto one  = __builtin_rvtt_sfpreadlreg (10);
      auto pair = __builtin_rvtt_sfpswap (w, one, 1);
      auto lo   = __builtin_rvtt_sfpselect2 (pair, 0);
      __builtin_rvtt_sfpstore (nullptr, lo, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void kd_ims_mve ()
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
      __builtin_rvtt_sfpstore (nullptr, w, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
