// Peel-class near misses on the canonical tail: the LAST CC writer of
// the body must be the WORD-EXACT all-lanes SFPENCC (capability table
// rvtt-macro-tables.cc sfpencc_all_lanes_word).  A trailing SFPENCC
// that clears the lane flags, and one that is all-lanes-equivalent but
// encodes a different word, both keep the plain sfpu-barrier refusal
// byte-identically.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "loop bb \\d+ refused .sfpu-barrier." 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "peeled first iteration" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void peel_dead_lanes_tail (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpencc (8, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void peel_noncanonical_word (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_sfpencc (3, 10);
    }
  __builtin_rvtt_sfpwritelreg (x, 1);
}
