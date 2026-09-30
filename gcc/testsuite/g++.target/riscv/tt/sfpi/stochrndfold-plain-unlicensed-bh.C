// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -fdump-tree-rvtt_store_fold" }
// The same plain float form keeps explicit rounding without the license.
// { dg-final { scan-tree-dump "store-fold refused .stochrnd-store-rounding-divergent" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "stochrnd-folded=0" "rvtt_store_fold" } }
// { dg-final { scan-assembler "SFPSTOCHRND" } }

__attribute__((noinline)) void
plain_stochrnd_unlicensed ()
{
  auto value = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto rounded = __builtin_rvtt_sfpstochrnd (value, 1, 0);
  __builtin_rvtt_sfpstore (nullptr, rounded, 0, 0, 0, 2, 7);
}
