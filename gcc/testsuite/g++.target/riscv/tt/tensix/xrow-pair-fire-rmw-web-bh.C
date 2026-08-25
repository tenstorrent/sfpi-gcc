// A row whose value web is a single accumulating (read-modify-write)
// register still pairs: RMW definitions cannot root a fresh rename web,
// but the copy's fresh load root renames and the interleave wins the
// modeled II.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "Crossrow pairing: bb \\d+ rows=2 nodes=16" "rvtt_schedule" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-7], 2, 0, 7} } }

void serial_web_row ()
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      x = __builtin_rvtt_sfpmad (x, x, x, 0);
      x = __builtin_rvtt_sfpmad (x, x, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      x = __builtin_rvtt_sfpmad (x, x, x, 0);
      __builtin_rvtt_sfppopc (0);
      x = __builtin_rvtt_sfpmad (x, x, x, 0);
      __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
