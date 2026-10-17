// Check that row-pair interleave zips two rows of a serial SFPMAD chain under one TTINCRWC(+4).
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-row-interleave -fdump-tree-rvtt_dst_row_interleave-details" }
// { dg-final { scan-tree-dump "Dst-row interleave: loop \\d+ trips 32 row-step 2 stmts \\d+ atoms 0 pair-cycles 20 -> 12 shared-words 0 evicted 0" "rvtt_dst_row_interleave" } }
// { dg-final { scan-tree-dump "Dst-row interleave: loop \\d+ rows paired, row step 2 -> 4" "rvtt_dst_row_interleave" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC\t0, 2, 0, 0" } }
// { dg-final { scan-assembler-times "SFPLOAD\tL\[0-7\], 2, 0, 7" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL\[0-7\], 2, 0, 7" 1 } }
// { dg-final { scan-assembler-times "SFPMAD" 8 } }

using vec_t = __xtt_vector;

void
horner_rows ()
{
  vec_t c0 = __builtin_rvtt_sfpreadlreg (12);
  vec_t c1 = __builtin_rvtt_sfpreadlreg (13);
  vec_t c2 = __builtin_rvtt_sfpreadlreg (14);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t r = __builtin_rvtt_sfpmad (x, c2, c1, 0);
      r = __builtin_rvtt_sfpmad (r, x, c0, 0);
      r = __builtin_rvtt_sfpmad (r, x, c1, 0);
      r = __builtin_rvtt_sfpmad (r, x, c2, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
