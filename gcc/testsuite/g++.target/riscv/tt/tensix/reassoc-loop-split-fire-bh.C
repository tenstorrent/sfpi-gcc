// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc-loop-carried -fdump-tree-rvtt_reassoc" }
// Check that a licensed loop-carried sfpadd accumulator splits into two partial accumulators with a reduction on the exit edge.
// { dg-final { scan-tree-dump-times "licensed loop-carried split P=2 over 2-link" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump "__builtin_rvtt_sfpreadlreg \\(9\\)" "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "refusing" "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "reassoc-loop-carried-underived" "rvtt_reassoc" } }

void
ra_loop_split_fire (int rows)
{
  auto acc = __builtin_rvtt_sfpxloadi (nullptr, 0x3f800000, 0, 0, -32);
  for (int i = 0; i != rows; ++i)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      auto y = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      acc = __builtin_rvtt_sfpadd (acc, x, 0);
      acc = __builtin_rvtt_sfpadd (acc, y, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 6, 7);
}
