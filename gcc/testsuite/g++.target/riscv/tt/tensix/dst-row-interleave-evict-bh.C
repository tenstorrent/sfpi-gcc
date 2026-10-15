// Row-pair interleave with an evicted invariant: six literal
// coefficients are materialized ahead of the loop and stay live across
// it, which leaves no room for a second row.  The cheapest-span invariants
// are re-issued inside the loop once per pair, just ahead of the two
// rows' reads, and the pair still issues in fewer modeled cycles than two
// rows with their exposed MAD stalls.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-row-interleave -fdump-tree-rvtt_dst_row_interleave-details" }
// { dg-final { scan-tree-dump "evicted \[1-9\] peak \[1-8\]\\n" "rvtt_dst_row_interleave" } }
// { dg-final { scan-tree-dump "rows paired, row step 2 -> 4" "rvtt_dst_row_interleave" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 4, 0, 0" 1 } }

using vec_t = __xtt_vector;

static inline vec_t
literal (unsigned hi, unsigned lo)
{
  vec_t v = __builtin_rvtt_sfploadi (nullptr, lo, 0, 0, 2);
  return __builtin_rvtt_sfploadi_lv (nullptr, v, hi, 0, 0, 8);
}

void
held_coefficients ()
{
  vec_t c0 = literal (0x3f80, 0x0000);
  vec_t c1 = literal (0x3f00, 0x1234);
  vec_t c2 = literal (0x3e80, 0x2345);
  vec_t c3 = literal (0x3e00, 0x3456);
  vec_t c4 = literal (0x3d80, 0x4567);
  vec_t c5 = literal (0x3d00, 0x5678);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t r = __builtin_rvtt_sfpmad (x, c5, c4, 0);
      r = __builtin_rvtt_sfpmad (r, x, c3, 0);
      r = __builtin_rvtt_sfpmad (r, x, c2, 0);
      r = __builtin_rvtt_sfpmad (r, x, c1, 0);
      r = __builtin_rvtt_sfpmad (r, x, c0, 0);
      __builtin_rvtt_sfpstore (nullptr, r, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
