// STORE-SOURCE TIER fire, post-CC peel class: the
// threshold/hardshrink shape measured on hardware -- the store-consumed
// constant materialization sits after the body's first CC writer, is
// admitted by the pressure-park consumer audit (SFPSTORE is in the
// audited lane-predicated set), and under
// -mtt-tensix-optimize-store-source-tier hoists to the peeled
// programming point as a plain LREG instead of parking in L12-L14
// where every predicated store row would pay the SFPMOV copy.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-store-source-tier -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "pressure-park: admitted post-CC candidate" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "store-source-tier .store-source-encoding-ceiling." 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "peeled first iteration" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "pressure-park: hoisted invariant materialization to a free LREG" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void tier_postcc_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      auto zero = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, zero, 0, 0, 0, 0, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
