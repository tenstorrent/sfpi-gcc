// Dst disjointness near miss: an address not proven 0 mod 4 refuses by
// name and the single row is kept byte-identically (one separator at the
// original advance, full trip count).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-crossrow-pairing -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "crossrow-pairing-dst-disjointness-unproven" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Crossrow pairing: bb" "rvtt_schedule" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC\t0, 4, 0, 0" } }

void unaligned_dst_row ()
{
  for (unsigned r = 0; r != 32; ++r)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 0, 7);
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
      __builtin_rvtt_sfpstore (nullptr, a, 2, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
