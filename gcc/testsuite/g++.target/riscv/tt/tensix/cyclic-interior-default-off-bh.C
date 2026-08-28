// Default-off proof: without -mtt-tensix-optimize-cyclic-region-schedule
// the fire kernel's self-loop keeps the plain list scheduler's original
// deferral wording -- the cyclic-interior machinery is invisible at
// every default.  Same kernel as the fire twin.
// (The scan-not keys on the machinery's "(cyclic-interior" spelling
// because this file's own NAME appears in the dump's source-location
// strings.)
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "List-schedule deferred: cyclic row adjacency in bb \\d+ \\(capture rotation owns the backedge seam\\)" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "\\(cyclic-interior" "rvtt_schedule" } }

void cis_fire_off ()
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
