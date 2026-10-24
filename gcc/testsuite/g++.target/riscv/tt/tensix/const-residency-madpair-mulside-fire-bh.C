// Check that a hoisted FLOATB mul constant moves to a PRGM register so the MUL and ADD fuse into SFPMAD.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .madpair class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "sfpmad" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPMAD" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-not "SFPMULI" } }

void madpair_mulside_fire (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto bias = __builtin_rvtt_sfpreadlreg (1);
  auto k = __builtin_rvtt_sfpxloadi (nullptr, 0x40400000, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, k, 0);
      x = __builtin_rvtt_sfpadd (prod, bias, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_stepped_scale (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto acc = __builtin_rvtt_sfpreadlreg (3);
  auto north = __builtin_rvtt_sfpreadlreg (4);
  auto w = __builtin_rvtt_sfpxloadi (nullptr, 0x41200000, 0, 0, 31);
  for (unsigned step = 0; step != 12; ++step)
    {
      auto term = __builtin_rvtt_sfpmul (acc, w, 0);
      acc = __builtin_rvtt_sfpadd (term, north, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 3);
}
