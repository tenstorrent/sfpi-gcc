// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-stochrnd-store-fold -fdump-tree-rvtt_store_fold" }
// A deterministic round still advances the PRNG.  A caller can consume the
// next draw after this callee returns, so the local license cannot delete it.
// { dg-final { scan-tree-dump "stochrnd-store-fold-entropy-stream" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "stochrnd-folded=0" "rvtt_store_fold" } }
// { dg-final { scan-assembler-times "SFPSTOCHRND" 2 } }
// { dg-final { scan-assembler "SFPSTORE" } }

__attribute__((noinline)) void
plain_stochrnd_fold ()
{
  auto value = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto rounded = __builtin_rvtt_sfpstochrnd (value, 1, 0);
  __builtin_rvtt_sfpstore (nullptr, rounded, 0, 0, 0, 2, 7);
}

__attribute__((noinline)) void
caller_entropy_consumer ()
{
  plain_stochrnd_fold ();
  auto value = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 6, 7);
  auto rounded = __builtin_rvtt_sfpstochrnd (value, 1, 1);
  __builtin_rvtt_sfpstore (nullptr, rounded, 64, 0, 0, 2, 7);
}
