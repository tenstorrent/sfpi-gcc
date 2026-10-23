// Check that a CC-canonical loop peels its first iteration and programs the constant after the peeled all-lanes SFPENCC.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "admits the CC-canonical peel" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "peeled first iteration" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// Only the peeled first iterations and the staging loads keep SFPLOADI pairs.
// { dg-final { scan-assembler-times "SFPLOADI" 8 } }

void peel_fire (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  __builtin_rvtt_sfpencc (10, 0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_varied_scale (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto west = __builtin_rvtt_sfpreadlreg (2);
  __builtin_rvtt_sfpencc (10, 0);
  for (unsigned step = 0; step != 12; ++step)
    {
      auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb, 0, 0, 31);
      west = __builtin_rvtt_sfpmul (west, bias, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (west, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (west, 2);
}
