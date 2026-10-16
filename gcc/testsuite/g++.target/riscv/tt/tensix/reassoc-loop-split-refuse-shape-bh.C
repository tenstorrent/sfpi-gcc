// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc-loop-carried -fdump-tree-rvtt_reassoc" }
// Check that the split refuses when the running sum is consumed inside the loop (reassoc-partials-loop-shape).
// { dg-final { scan-tree-dump-times "reassoc-partials-loop-shape" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "licensed loop-carried split" "rvtt_reassoc" } }

void
ra_loop_split_shape (int rows)
{
  auto acc = __builtin_rvtt_sfpxloadi (nullptr, 0x3f800000, 0, 0, -32);
  for (int i = 0; i != rows; ++i)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      auto y = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      acc = __builtin_rvtt_sfpadd (acc, x, 0);
      acc = __builtin_rvtt_sfpadd (acc, y, 0);
      __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 6, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 6, 7);
}
