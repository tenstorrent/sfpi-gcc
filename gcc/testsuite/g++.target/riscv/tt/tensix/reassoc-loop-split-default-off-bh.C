// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_reassoc" }
// Token-absent control for the item-#8 fire body: without
// -mtt-tensix-optimize-reassoc-loop-carried the standing
// reassoc-loop-carried-underived refusal continues byte-identically
// (the historical diagnostic walk, kept verbatim) and nothing splits.
// { dg-final { scan-tree-dump "reassoc-loop-carried-underived" "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "loop-carried split P" "rvtt_reassoc" } }

void
ra_loop_split_off (int rows)
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
