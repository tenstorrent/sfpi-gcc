// Check that shared-reload dedupe refuses by name when the copy half's reload web was renamed.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-crossrow-pairing-stall-words -mtt-tensix-optimize-crossrow-shared-reload -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump {crossrow-shared-reload-copy-shape reg \d+} "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Crossrow shared-reload: reg" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump {Crossrow pairing: bb \d+ rows=2} "rvtt_schedule" } }

void low_pressure_reload_row (void)
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0xbefa66db, 0, 0, 31);
      auto p = __builtin_rvtt_sfpmad (x, x, c3, 0);
      auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f7fbec0, 0, 0, 31);
      p = __builtin_rvtt_sfpmad (p, x, c1, 0);
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
