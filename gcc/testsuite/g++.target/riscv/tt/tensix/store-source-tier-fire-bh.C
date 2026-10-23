// Check that a store-consumed loop constant is hoisted to a plain LREG (SFPSTORE sources L0-L11 only).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-store-source-tier -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "store-source-tier .store-source-encoding-ceiling." 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "pressure-park: hoisted invariant materialization to a free LREG" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// Only the two explicit LaneConfig resets; no PRGM programming.
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }
// { dg-final { scan-assembler-not "SFPMOV" } }

void tier_loop_fire (void)
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto fill = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, fill, 0, 0, 0, 0, 0);
    }
}

void renamed_varied_splat (void)
{
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  for (unsigned step = 0; step != 12; ++step)
    {
      auto seed = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, seed, 0, 0, 0, 0, 2);
    }
}
