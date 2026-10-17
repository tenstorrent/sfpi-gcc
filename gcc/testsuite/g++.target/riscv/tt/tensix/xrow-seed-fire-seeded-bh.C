// Check that the pairing seed sub-flag emits an SFPMOV preservation seed for a predicated root inside a CC atom.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-crossrow-pairing-seed -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump {Crossrow pairing seed: reg \d+ -> \d+ web at uid=\d+ \(\d+ insns\) seed uid=\d+ II \d+ -> \d+} "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times {Crossrow pairing seed refused: crossrow-pairing-seed-no-ii-improvement} 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump {Crossrow pairing: bb \d+ rows=2 nodes=\d+ II \d+ -> \d+ renames=\d+ seeds=1} "rvtt_schedule" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC\t0, 2, 0, 0" } }

void seeded_root_row ()
{
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
