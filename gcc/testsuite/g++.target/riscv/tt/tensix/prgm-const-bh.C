// Check that a loop-invariant SFPADDI immediate is programmed into a PRGM register and the pair fuses to SFPMAD.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "prgm-const: allocated PRGM L\\d+ for invariant immediate" 2 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }
// { dg-final { scan-assembler-not "SFPADDI" } }
// { dg-final { scan-assembler "SFPMAD" } }

void prgm_const_fire ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_scaled_bias ()
{
  auto north = __builtin_rvtt_sfpreadlreg (2);
  auto gain = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned step = 0; step != 12; ++step)
    {
      auto blended = __builtin_rvtt_sfpmul (north, gain, 0);
      north = __builtin_rvtt_sfpaddi (nullptr, blended, 0x3f81, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (north, 2);
}
