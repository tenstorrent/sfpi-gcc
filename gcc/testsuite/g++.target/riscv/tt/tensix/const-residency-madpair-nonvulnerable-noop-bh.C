// MAD-PAIR no-op on non-vulnerable constants: sfpxloadi chain forms
// (low bits set, not FLOATB-representable) are never matched by the
// muli/addi immediate folds, so the mad rule fuses the hoisted pair
// exactly as it stands -- there is no decay and the class must not
// spend a PRGM register or a programming word on it.
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
