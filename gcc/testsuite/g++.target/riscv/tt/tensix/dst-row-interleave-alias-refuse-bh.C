// Check that row-pair interleave leaves a loop alone when each row writes the row the next trip reads.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-row-interleave -fdump-tree-rvtt_dst_row_interleave-details" }
// { dg-final { scan-tree-dump "refused: loop \\d+ \\(dst-row-interleave-dst-alias\\)" "rvtt_dst_row_interleave" } }
// { dg-final { scan-tree-dump-not "rows paired" "rvtt_dst_row_interleave" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }

using vec_t = __xtt_vector;

void
forward_rows ()
{
  vec_t c0 = __builtin_rvtt_sfpreadlreg (12);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t r = __builtin_rvtt_sfpmad (x, x, c0, 0);
      r = __builtin_rvtt_sfpmad (r, x, c0, 0);
      r = __builtin_rvtt_sfpmad (r, x, c0, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 2, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
