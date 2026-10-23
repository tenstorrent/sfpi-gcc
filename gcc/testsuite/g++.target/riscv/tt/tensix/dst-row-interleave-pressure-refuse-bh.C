// Check that row-pair interleave refuses when two rows need more than eight LREGs.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-row-interleave -fdump-tree-rvtt_dst_row_interleave-details" }
// { dg-final { scan-tree-dump "pair peak \\d+ exceeds the 8-LREG file" "rvtt_dst_row_interleave" } }
// { dg-final { scan-tree-dump "refused: loop \\d+ \\(dst-row-interleave-pressure-exceeded\\)" "rvtt_dst_row_interleave" } }
// { dg-final { scan-tree-dump-not "rows paired" "rvtt_dst_row_interleave" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }

using vec_t = __xtt_vector;

void
wide_rows ()
{
  // Lane flags and LaneConfig ROW_MASK are independent hardware state.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t a = __builtin_rvtt_sfpmul (x, x, 0);
      vec_t b = __builtin_rvtt_sfpmul (a, x, 0);
      vec_t c = __builtin_rvtt_sfpmul (b, x, 0);
      vec_t d = __builtin_rvtt_sfpmul (c, x, 0);
      vec_t e = __builtin_rvtt_sfpmul (d, x, 0);
      vec_t r = __builtin_rvtt_sfpmad (a, b, c, 0);
      r = __builtin_rvtt_sfpmad (r, d, e, 0);
      r = __builtin_rvtt_sfpmad (r, x, a, 0);
      r = __builtin_rvtt_sfpmad (r, b, c, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
