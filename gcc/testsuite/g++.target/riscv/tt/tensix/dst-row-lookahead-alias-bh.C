// Check that row look-ahead does not move a load above a possibly aliasing store.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-row-lookahead -fdump-tree-rvtt_dst_row_lookahead-details" }
// { dg-final { scan-tree-dump "Dst-row lookahead refused: \\d+ \\(dst-row-lookahead-dst-alias\\)" "rvtt_dst_row_lookahead" } }
// { dg-final { scan-tree-dump-not "peeled last row" "rvtt_dst_row_lookahead" } }
// { dg-final { scan-assembler-times "SFPLOAD" 1 } }

using vec_t = __xtt_vector;

void
store_then_load ()
{
  vec_t c0 = __builtin_rvtt_sfpreadlreg (12);
  vec_t c1 = __builtin_rvtt_sfpreadlreg (13);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      __builtin_rvtt_sfpstore (nullptr, c1, 4, 0, 0, 0, 7);
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t r = __builtin_rvtt_sfpmad (x, c0, c1, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
