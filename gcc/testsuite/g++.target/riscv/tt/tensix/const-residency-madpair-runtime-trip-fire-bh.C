// Check that the MAD-pair re-claim fires with a runtime trip count.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .madpair class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "trip-count-single-trip" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPMAD" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-not "SFPADDI" } }

void madpair_runtime_trips (unsigned n)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e2aaaab, 0, 0, 31);
  auto half = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
  for (unsigned ix = 0; ix != n; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, gain, 0);
      x = __builtin_rvtt_sfpadd (prod, half, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_scaled_ramp (int rows)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  auto slope = __builtin_rvtt_sfpxloadi (nullptr, 0x3ea8f5c3, 0, 0, 31);
  auto lift = __builtin_rvtt_sfpxloadi (nullptr, 0x40a00000, 0, 0, 31);
  for (int r = 0; r < rows; ++r)
    {
      auto term = __builtin_rvtt_sfpmul (acc, slope, 0);
      acc = __builtin_rvtt_sfpadd (term, lift, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
