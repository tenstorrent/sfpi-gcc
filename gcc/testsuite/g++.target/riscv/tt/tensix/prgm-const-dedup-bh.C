// Identical-immediate reuse: two candidate loops with the SAME fp32
// immediate share one PRGM register; when the first programming point
// dominates the second loop, no second programming write is emitted
// (one SFPCONFIG total).  The varied twin uses two DIFFERENT
// immediates and burns two registers (two SFPCONFIG writes).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "prgm-const: allocated PRGM L\\d+ for invariant immediate" 3 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "prgm-const: reused PRGM L\\d+ for identical immediate" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 3 } }

void two_loops_one_immediate ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);

  auto y = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned iy = 0; iy != 16; ++iy)
    {
      auto prod = __builtin_rvtt_sfpmul (y, s, 0);
      y = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (y, 2);
}

void varied_two_immediates ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x3f81, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);

  auto y = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned iy = 0; iy != 16; ++iy)
    {
      auto prod = __builtin_rvtt_sfpmul (y, s, 0);
      y = __builtin_rvtt_sfpaddi (nullptr, prod, 0x4040, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (y, 2);
}
