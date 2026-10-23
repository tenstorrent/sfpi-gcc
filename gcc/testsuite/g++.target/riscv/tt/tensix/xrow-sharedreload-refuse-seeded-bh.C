// Check that shared-reload dedupe refuses by name a row grown by a pairing seed.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-crossrow-pairing-seed -mtt-tensix-optimize-crossrow-shared-reload -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump {crossrow-shared-reload-seeded-row} "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Crossrow shared-reload: reg" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump {Crossrow pairing: bb \d+ rows=2 nodes=\d+ II \d+ -> \d+ renames=\d+ seeds=1} "rvtt_schedule" } }

void seeded_root_row ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto e = __builtin_rvtt_sfpexexp (x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (e, 0);
      auto c = __builtin_rvtt_sfpassign_lv (x, x);
      c = __builtin_rvtt_sfpadd (c, x, 0);
      __builtin_rvtt_sfppopc (0);
      c = __builtin_rvtt_sfpmad (c, x, x, 0);
      c = __builtin_rvtt_sfpmad (c, x, x, 0);
      c = __builtin_rvtt_sfpmad (c, x, x, 0);
      __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
