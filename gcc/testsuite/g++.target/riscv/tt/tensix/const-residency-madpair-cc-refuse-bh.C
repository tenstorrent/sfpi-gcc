// Check that a CC write reaching a hoisted MAD-pair constant refuses the re-claim (cc-region-unproven).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "madpair candidate in bb \\d+ refused .cc-region-unproven" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "madpair class" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPADDI" 1 } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void madpair_cc_refuse (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc (x, 0);
  __builtin_rvtt_sfppopc (0);
  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e2aaaab, 0, 0, 31);
  auto half = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, gain, 0);
      x = __builtin_rvtt_sfpadd (prod, half, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
