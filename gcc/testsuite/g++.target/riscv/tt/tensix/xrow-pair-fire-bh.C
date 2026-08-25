// Cross-row pairing fire: a capturable Dst row loop with flat all-lanes-
// restored CC atoms pairs two iterations -- the copy's Dst accesses rebase
// 0 -> 2, the shared separator doubles to (0, 4), the countdown halves.
// No function or opcode identity participates.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "Crossrow pairing: bb \\d+ rows=2" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "trips=32->16" "rvtt_schedule" } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-7], 2, 0, 7} } }
// { dg-final { scan-assembler {SFPSTORE\tL[0-7], 2, 0, 7} } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC\t0, 2, 0, 0" } }

void paired_dst_row ()
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto a = __builtin_rvtt_sfpabs (x, 1);
      auto b = __builtin_rvtt_sfpmul (a, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (b, 0);
      a = __builtin_rvtt_sfpadd (a, x, 0);
      __builtin_rvtt_sfppopc (0);
      b = __builtin_rvtt_sfpmad (a, b, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (b, 0);
      a = __builtin_rvtt_sfpxor (a, b);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpstore (nullptr, a, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
