// WH arm of park-prepeel-lreg-fire-bh.C: the pre-peel park placement
// is architecture-independent (the ambient proof and the peel-copy
// erase are gimple facts; the canonical all-lanes SFPENCC word is the
// shared capability-table derivation on both).  Names, constants and
// trip count varied.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "pressure-park: admitted post-CC candidate" 4 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 3 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "pressure-park: hoisted invariant materialization to a free LREG at the pre-peel entry .peel bb \\d+; ambient all-lanes proven; peel duplicate erased." 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "free LREG at the programming point" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 3 } }

void wharf_prepeel_scale (void)
{
  auto north = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned step = 0; step != 24; ++step)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (north, 0);
      __builtin_rvtt_sfppopc (0);
      auto k0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3d8f5db9, 0, 0, 31);
      auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3ea7c04f, 0, 0, 31);
      auto k2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb504f3, 0, 0, 31);
      auto k3 = __builtin_rvtt_sfpxloadi (nullptr, 0x4066e979, 0, 0, 31);
      north = __builtin_rvtt_sfpmul (north, k0, 0);
      north = __builtin_rvtt_sfpmul (north, k1, 0);
      north = __builtin_rvtt_sfpmul (north, k2, 0);
      north = __builtin_rvtt_sfpmul (north, k3, 0);
    }
  __builtin_rvtt_sfpwritelreg (north, 2);
}
