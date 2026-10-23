// Check that -mtt-tensix-optimize-madpair-vocabulary re-claims a mul constant feeding the add through an SFPMOV complement.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-madpair-vocabulary -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "madpair loop bb \\d+ candidate: hoisted constant" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .madpair class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "sfpmad" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPMAD" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-not "SFPMULI" } }

void vocab_compl_fire (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto twice = __builtin_rvtt_sfpxloadi (nullptr, 0x40000000, 0, 0, 31);
  auto lift = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, twice, 0);
      auto neg = __builtin_rvtt_sfpmov (prod, 1);
      x = __builtin_rvtt_sfpadd (lift, neg, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_edge_ramp (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  auto slope = __builtin_rvtt_sfpxloadi (nullptr, 0x3f400000, 0, 0, 31);
  auto bias = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned row = 0; row != 8; ++row)
    {
      auto scaled = __builtin_rvtt_sfpmul (acc, slope, 0);
      auto flipped = __builtin_rvtt_sfpmov (scaled, 1);
      acc = __builtin_rvtt_sfpadd (bias, flipped, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (acc, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
