// Check that without the store-source tier a store-consumed PRGM constant needs a per-row SFPMOV copy.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "store-source-tier" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x3e4b1a3d .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPCONFIG" } }
// { dg-final { scan-assembler "SFPMOV" } }

void tier_off_tax (void)
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto fill = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, fill, 0, 0, 0, 0, 0);
    }
}
