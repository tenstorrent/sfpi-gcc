// Unknown entry, flags-only enable and nonzero ROW_MASK must not pair rows.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-crossrow-pairing -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "crossrow-pairing-entry-cc-unproven" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "Crossrow pairing: bb" "rvtt_schedule" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 3 } }
// { dg-final { scan-assembler-not "TTINCRWC\t0, 4, 0, 0" } }

__attribute__((always_inline)) static inline void
row_body ()
{
  for (unsigned row = 0; row != 32; ++row)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto a = __builtin_rvtt_sfpabs (x, 1);
      auto b = __builtin_rvtt_sfpmul (a, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (b, 0);
      a = __builtin_rvtt_sfpadd (a, x, 0);
      __builtin_rvtt_sfppopc (0);
      b = __builtin_rvtt_sfpmad (a, b, x, 0);
      __builtin_rvtt_sfpstore (nullptr, b, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void unknown_entry ()
{
  row_body ();
}

void flags_only ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  row_body ();
}

void row_mask_not_zero ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (1, 15, 1);
  row_body ();
}
