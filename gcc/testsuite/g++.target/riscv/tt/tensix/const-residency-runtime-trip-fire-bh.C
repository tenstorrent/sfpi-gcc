// Check that loop-class residency admits a runtime trip count.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "admits runtime trips" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "const-residency: allocated PRGM L1\\d for constant" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "trip-count" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }

void residency_runtime_trips (unsigned n)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != n; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_scaled_rows (int rows)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto acc = __builtin_rvtt_sfpreadlreg (4);
  for (int r = 0; r < rows; ++r)
    {
      auto w = __builtin_rvtt_sfpxloadi (nullptr, 0x3f4ccccd, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, w, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 4);
}
