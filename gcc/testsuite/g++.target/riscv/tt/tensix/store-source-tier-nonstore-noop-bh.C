// Check that the store-source tier leaves a constant consumed only by SFPMUL in its PRGM park.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-store-source-tier -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "store-source-tier" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x3e4b1a3d .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPCONFIG" } }
// The accumulator's own SFPMOVs are fine; no copy out of the constant file.
// { dg-final { scan-assembler-not "SFPMOV\tL\[0-9\]+, L1\[2-4\]" } }

void tier_nonstore_noop (void)
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
