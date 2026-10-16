// Check that row look-ahead pipelines a 32-row loop by one row without loading past the end.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-row-lookahead -fdump-tree-rvtt_dst_row_lookahead-details" }
// { dg-final { scan-tree-dump "Dst-row lookahead: loop \\d+ trips 32 -> 31 and a peeled last row, 1 statement\\(s\\)" "rvtt_dst_row_lookahead" } }
// { dg-final { scan-assembler-times "SFPLOAD\tL\[0-7\], 0, 0, 7" 1 } }
// { dg-final { scan-assembler-times "SFPLOAD\tL\[0-7\], 2, 0, 7" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL\[0-7\], 0, 0, 7" 2 } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 2 } }

using vec_t = __xtt_vector;

void
row_loop ()
{
  vec_t c0 = __builtin_rvtt_sfpreadlreg (12);
  vec_t c1 = __builtin_rvtt_sfpreadlreg (13);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t r = __builtin_rvtt_sfpmad (x, c0, c1, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
