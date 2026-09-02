// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc-loop-carried -fdump-tree-rvtt_reassoc" }
// Arch breadth for the accumulator-splitting fire: the loop-carried split is
// target-independent gimple (charter: >= 2 unrelated shapes/targets).
// WH load/store addr-mode 3.
// { dg-final { scan-tree-dump-times "licensed loop-carried split P=2 over 2-link" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "refusing" "rvtt_reassoc" } }

void
ra_loop_split_fire_wh (int rows)
{
  auto acc = __builtin_rvtt_sfpxloadi (nullptr, 0x3f800000, 0, 0, -32);
  for (int i = 0; i != rows; ++i)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 3);
      auto y = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 3);
      acc = __builtin_rvtt_sfpadd (acc, x, 0);
      acc = __builtin_rvtt_sfpadd (acc, y, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 6, 3);
}
