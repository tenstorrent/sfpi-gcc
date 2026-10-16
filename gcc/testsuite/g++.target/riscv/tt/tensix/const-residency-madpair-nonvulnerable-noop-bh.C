// Check that MAD-pair constants that are not FLOATB-representable are not moved to PRGM registers.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "madpair" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPMAD" 1 } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void nonvulnerable_pair_noop (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e2aaaab, 0, 0, 31);
  auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, gain, 0);
      x = __builtin_rvtt_sfpadd (prod, bias, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
