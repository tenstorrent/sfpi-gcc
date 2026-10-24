// Check that SFPADD constants loaded by sfpxloadi mod 31 are allocated and that the pass does not fuse mul and add.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "prgm-const: allocated PRGM L1\\d for invariant immediate" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "sfpmad" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-times "SFPMAD" 2 } }

void muladd_fire ()
{
  // Both predicate flags and ROW_MASK are unknown at entry.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto k = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb8aa3b, 0, 0, 31);
      auto prod = __builtin_rvtt_sfpmul (x, k, 0);
      auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
      x = __builtin_rvtt_sfpadd (prod, b, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_offset_blend ()
{
  // Both predicate flags and ROW_MASK are unknown at entry.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto acc = __builtin_rvtt_sfpreadlreg (4);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto g = __builtin_rvtt_sfpxloadi (nullptr, 0x3f0f5c29, 0, 0, 31);
      auto scaled = __builtin_rvtt_sfpmul (acc, g, 0);
      auto off = __builtin_rvtt_sfpxloadi (nullptr, 0x3d4ccccd, 0, 0, 31);
      acc = __builtin_rvtt_sfpadd (scaled, off, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 4);
}
