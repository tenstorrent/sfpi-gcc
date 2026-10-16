// Check that a MAD-pair constant with uses beyond the pair refuses the re-claim (madpair-shared-constant).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "madpair-shared-constant" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "madpair class" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPADDI" 2 } }
// { dg-final { scan-assembler-not "SFPMAD\[^I\]" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void madpair_shared_refuse (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto y = __builtin_rvtt_sfpreadlreg (1);
  auto g1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e2aaaab, 0, 0, 31);
  auto g2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3ea8f5c3, 0, 0, 31);
  auto half = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto p1 = __builtin_rvtt_sfpmul (x, g1, 0);
      x = __builtin_rvtt_sfpadd (p1, half, 0);
      auto p2 = __builtin_rvtt_sfpmul (y, g2, 0);
      y = __builtin_rvtt_sfpadd (p2, half, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpwritelreg (y, 1);
}
