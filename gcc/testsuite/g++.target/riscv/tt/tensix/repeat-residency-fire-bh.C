// A row loop unrolled by its pragma inside a loop-free body: the same
// 32-bit coefficient is materialized on every row.  The repeat class
// programs one PRGM register at the function entry and every row reads
// it (one staged pair and one SFPCONFIG instead of eight pairs).
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-repeat-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "const-residency: repeat group 0x3e4b1a3d: 8 materializations read PRGM L1\\d, programmed once at the function entry" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }
// { dg-final { scan-assembler-times "SFPLOADI" 2 } }

__attribute__((noinline)) void repeat_rows (void)
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
