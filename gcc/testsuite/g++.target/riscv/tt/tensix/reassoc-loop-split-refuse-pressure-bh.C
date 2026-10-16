// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc-loop-carried -fdump-tree-rvtt_reassoc" }
// Check that the accumulator split refuses when no LREG is free for an extra partial (reassoc-partials-pressure).
// { dg-final { scan-tree-dump-times "reassoc-partials-pressure" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "licensed loop-carried split" "rvtt_reassoc" } }

void
ra_loop_split_pressure (int rows)
{
  auto k0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto k1 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto k2 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto k3 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto k4 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto k5 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto acc = __builtin_rvtt_sfpxloadi (nullptr, 0x3f800000, 0, 0, -32);
  for (int i = 0; i != rows; ++i)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      acc = __builtin_rvtt_sfpadd (acc, x, 0);
      acc = __builtin_rvtt_sfpadd (acc, x, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpstore (nullptr, acc, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k0, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k1, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k2, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k3, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k4, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k5, 0, 0, 0, 6, 7);
}
