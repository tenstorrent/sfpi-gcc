// Check that with the stall-words sub-flag a row ending in SFPSWAP pairs and fills the SFPMUL->SFPSWAP shadow.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -mtt-tensix-optimize-crossrow-pairing-stall-words -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "Crossrow pairing: bb \\d+ rows=2" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "trips=32->16" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "crossrow-pairing-effect-unproven" "rvtt_schedule" } }
// { dg-final { scan-assembler "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPNOP" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }

void swap_tail_row ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto a = __builtin_rvtt_sfpabs (x, 1);
      auto p = __builtin_rvtt_sfpmul (a, x, 0);
      auto one = __builtin_rvtt_sfpreadlreg (10);
      auto sw = __builtin_rvtt_sfpswap (p, one, 1);
      auto mn = __builtin_rvtt_sfpselect2 (sw, 0);
      auto res = __builtin_rvtt_sfpsetsgn_v (mn, x, 0);
      __builtin_rvtt_sfpstore (nullptr, res, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
