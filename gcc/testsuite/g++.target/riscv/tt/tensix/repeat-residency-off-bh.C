// Without -mtt-tensix-optimize-repeat-residency the unrolled rows keep
// their own materializations.
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "repeat group" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

__attribute__((noinline)) void repeat_rows_off (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
#pragma GCC unroll 8
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
