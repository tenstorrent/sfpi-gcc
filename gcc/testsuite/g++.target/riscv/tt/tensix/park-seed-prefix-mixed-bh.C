// PARK-SEED COMPOSITION mixed split (lane HY): ONE CC-restore loop
// carrying BOTH candidate classes.  The DEPTH-ZERO invariant immediate
// keeps the early hoist (depth-zero-hoist-dominant); the IN-REGION one
// defers to the late const-residency walk (residency-walk-ordering),
// which parks it under its audited post-CC admission.  Distinct
// constants so the walk's value-dedup cannot blur the split.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -fdump-tree-rvtt_invariant-details -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "hoist kept under park-ordering: depth-zero-hoist-dominant" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "hoist deferred: residency-walk-ordering" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x40490fdb .loop class" 1 "rvtt_prgm_const" } }

void mixed_prefix_and_postcc (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto magic = __builtin_rvtt_sfpxloadi (nullptr, 0x4b400000, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, magic, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      __builtin_rvtt_sfppopc (0);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
