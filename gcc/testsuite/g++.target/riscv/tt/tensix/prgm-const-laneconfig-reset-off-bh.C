// Default-off: without -mtt-tensix-optimize-prgm-const the TU with
// the LaneConfig default-reset word keeps its SFPADDI and no
// SFPCONFIG mnemonic appears (the reset stays a raw .ttinsn word).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-final { scan-assembler "SFPADDI" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler "\\.ttinsn" } }

void sfpu_lane_reset_init ()
{
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x910000F1));
}

void prgm_reset_fire ()
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
