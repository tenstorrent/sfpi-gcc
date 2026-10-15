// Row-pair interleave CC refusals.  open_rows sets CC inside the row and
// never restores all lanes, so the lane state one row leaves behind is
// the next row's input: zipping would run row B's words under row A's
// predicate.  predicated_entry enters the loop with lanes disabled, so
// the ambient state the zip would move row B's words into is not proven
// all-lanes.  Both loops are left untouched.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-row-interleave -fdump-tree-rvtt_dst_row_interleave-details" }
// { dg-final { scan-tree-dump "refused: loop \\d+ \\(dst-row-interleave-cc-unclosed\\)" "rvtt_dst_row_interleave" } }
// { dg-final { scan-tree-dump "refused: loop \\d+ \\(dst-row-interleave-entry-cc-unproven\\)" "rvtt_dst_row_interleave" } }
// { dg-final { scan-tree-dump-not "rows paired" "rvtt_dst_row_interleave" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 2 } }

using vec_t = __xtt_vector;

void
open_rows ()
{
  vec_t c0 = __builtin_rvtt_sfpreadlreg (12);
  __builtin_rvtt_sfppushc (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfpsetcc (x, 0);
      vec_t r = __builtin_rvtt_sfpmad (x, x, c0, 0);
      r = __builtin_rvtt_sfpmad (r, x, c0, 0);
      r = __builtin_rvtt_sfpmad (r, x, c0, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfppopc (0);
}

void
predicated_entry ()
{
  vec_t c0 = __builtin_rvtt_sfpreadlreg (12);
  vec_t m = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc (m, 0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t r = __builtin_rvtt_sfpmad (x, x, c0, 0);
      r = __builtin_rvtt_sfpmad (r, x, c0, 0);
      r = __builtin_rvtt_sfpmad (r, x, c0, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfppopc (0);
}
