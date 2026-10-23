// Check that the CC-canonical peel refuses a runtime trip count and a trip count below break-even.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "refused .peel-trip-count-unproven" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "peeled first iteration" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

void peel_trip_unproven (unsigned n)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (1);
  __builtin_rvtt_sfpencc (10, 0);
  for (unsigned ix = 0; ix != n; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 1);
}

void peel_two_trips (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  __builtin_rvtt_sfpencc (10, 0);
  for (unsigned ix = 0; ix != 2; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}
