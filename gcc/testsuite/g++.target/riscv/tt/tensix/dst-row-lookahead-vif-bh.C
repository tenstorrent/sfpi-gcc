// Check that row look-ahead moves the next row's load only after the all-lanes SFPENCC.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-row-lookahead -fdump-tree-rvtt_dst_row_lookahead-details" }
// { dg-final { scan-tree-dump "Dst-row lookahead: loop \\d+ trips 32 -> 31 and a peeled last row" "rvtt_dst_row_lookahead" } }
// { dg-final { scan-assembler "SFPENCC\[^\n\]*\n(\[^\n\]*SFPMAD\[^\n\]*\n)?\[^\n\]*SFPLOAD\tL\[0-7\], 2, 0, 7" } }
// { dg-final { scan-assembler-times "SFPLOAD" 2 } }

using vec_t = __xtt_vector;

void
guarded_rows ()
{
  vec_t c0 = __builtin_rvtt_sfpreadlreg (12);
  vec_t c1 = __builtin_rvtt_sfpreadlreg (13);
  __builtin_rvtt_sfppushc (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t r = __builtin_rvtt_sfpmad (x, c0, c1, 0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfpencc (10, 3);
      vec_t s = __builtin_rvtt_sfpmad (r, x, c1, 0);
      __builtin_rvtt_sfpstore (nullptr, s, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfppopc (0);
}
