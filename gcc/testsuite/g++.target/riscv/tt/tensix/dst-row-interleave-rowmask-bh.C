// Row pairing must not infer full lanes from unknown entry or SFPENCC alone.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-row-interleave -fdump-tree-rvtt_dst_row_interleave-details" }
// { dg-final { scan-tree-dump-times "dst-row-interleave-entry-cc-unproven" 3 "rvtt_dst_row_interleave" } }
// { dg-final { scan-tree-dump-not "rows paired, row step 2 -> 4" "rvtt_dst_row_interleave" } }

using vec_t = __xtt_vector;

#define ROWS()                                                            \
  for (unsigned ix = 0; ix != 32; ++ix)                                    \
    {                                                                    \
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);            \
      vec_t c = __builtin_rvtt_sfpreadlreg (12);                            \
      vec_t r = __builtin_rvtt_sfpmad (x, x, c, 0);                         \
      r = __builtin_rvtt_sfpmad (r, x, c, 0);                              \
      r = __builtin_rvtt_sfpmad (r, x, c, 0);                              \
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);                  \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                \
    }

void unknown_entry ()
{
  ROWS ();
}

void flags_without_rowmask_proof ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  ROWS ();
}

void configured_nonzero_rowmask ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (1, 15, 1);
  ROWS ();
}
