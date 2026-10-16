// Check that row look-ahead moves the next row's load and MAD ahead of the row step in straight-line rows.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-row-lookahead -fdump-tree-rvtt_dst_row_lookahead-details" }
// { dg-final { scan-tree-dump-times "Dst-row lookahead: bb \\d+ row \\d+: 2 statement\\(s\\) of the next row" 3 "rvtt_dst_row_lookahead" } }
// { dg-final { scan-assembler-times "SFPLOAD\tL\[0-7\], 0, 0, 7" 1 } }
// { dg-final { scan-assembler-times "SFPLOAD\tL\[0-7\], 2, 0, 7" 3 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL\[0-7\], 0, 0, 7" 4 } }

using vec_t = __xtt_vector;

#define ROW()								\
  do									\
    {									\
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);	\
      vec_t r = __builtin_rvtt_sfpmad (x, c0, c1, 0);			\
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);		\
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);				\
    }									\
  while (0)

void
four_rows ()
{
  vec_t c0 = __builtin_rvtt_sfpreadlreg (12);
  vec_t c1 = __builtin_rvtt_sfpreadlreg (13);
  ROW ();
  ROW ();
  ROW ();
  ROW ();
}
