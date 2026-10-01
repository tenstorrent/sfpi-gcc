// MVE realization Wormhole inertness: the realization rides the
// cross-row pairing's counted-kernel seam, and the pairing is
// Blackhole-only (its audited latency/adjacency model family) -- on WH
// the pairing refuses by name before any realization arm can run, and
// no mve-expand line of any kind appears.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-rename-temporal -mtt-tensix-optimize-mve-expand -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "crossrow-pairing-bh-only" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Crossrow mve-expand" "rvtt_schedule" } }

void mve_wh_row ()
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 3);
      auto v1 = __builtin_rvtt_sfpmul (x, x, 0);
      auto c1 = __builtin_rvtt_sfpmul (x, x, 0);
      auto c2 = __builtin_rvtt_sfpmad (c1, x, x, 0);
      auto res = __builtin_rvtt_sfpmad (c2, v1, x, 0);
      __builtin_rvtt_sfpstore (nullptr, res, 0, 0, 0, 0, 3);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
