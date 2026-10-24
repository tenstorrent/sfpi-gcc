// Check that row-pair interleave refuses a row with no exposed result latency.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-row-interleave -fdump-tree-rvtt_dst_row_interleave-details" }
// { dg-final { scan-tree-dump "pair-cycles 6 -> 6" "rvtt_dst_row_interleave" } }
// { dg-final { scan-tree-dump "refused: loop \\d+ \\(dst-row-interleave-no-benefit\\)" "rvtt_dst_row_interleave" } }
// { dg-final { scan-tree-dump-not "rows paired" "rvtt_dst_row_interleave" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }

using vec_t = __xtt_vector;

void
abs_rows ()
{
  // Lane flags and LaneConfig ROW_MASK are independent hardware state.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfpstore (nullptr, __builtin_rvtt_sfpabs (x, 1),
			       0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
