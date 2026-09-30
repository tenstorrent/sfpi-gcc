// MAD-PAIR named refusal, prgm-exhausted, and the pair-atomic
// placement: two pairs whose constants are all fold-vulnerable need
// two PRGM registers each; the first group claims two of the three,
// the second refuses whole (a half-claimed pair would pay its
// programming word while the surviving immediate fold still blocks the
// mad rule).  The refused pair's mul and add keep their immediate
// folds (SFPMULI + SFPADDI in the final assembly).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "madpair-prgm-exhausted" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .madpair class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPMAD" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }
// { dg-final { scan-assembler-times "SFPMULI" 1 } }
// { dg-final { scan-assembler-times "SFPADDI" 1 } }

void madpair_prgm_exhausted (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto y = __builtin_rvtt_sfpreadlreg (1);
  auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x40400000, 0, 0, 31);
  auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
  auto k2 = __builtin_rvtt_sfpxloadi (nullptr, 0x41200000, 0, 0, 31);
  auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x40a00000, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto p1 = __builtin_rvtt_sfpmul (x, k1, 0);
      x = __builtin_rvtt_sfpadd (p1, c1, 0);
      auto p2 = __builtin_rvtt_sfpmul (y, k2, 0);
      y = __builtin_rvtt_sfpadd (p2, c2, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpwritelreg (y, 1);
}
