// WH inertness: the shared-reload dedupe lives inside the cross-row
// pairing transaction, and the pairing is Blackhole-only -- on
// Wormhole the full composed flag set leaves the single row exactly
// (no pairing, no dedupe, single-step separator).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-crossrow-pairing-stall-words -mtt-tensix-optimize-crossrow-shared-reload -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "crossrow-pairing-bh-only" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Crossrow shared-reload" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Crossrow pairing: bb" "rvtt_schedule" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }

void reload_row (void)
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 3);
      auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0xbefa66db, 0, 0, 31);
      auto p = __builtin_rvtt_sfpmad (x, x, c3, 0);
      auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f7fbec0, 0, 0, 31);
      p = __builtin_rvtt_sfpmad (p, x, c1, 0);
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 3);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
