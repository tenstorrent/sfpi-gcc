// Default-off: without -mtt-tensix-optimize-prgm-const the fire shape
// from prgm-const-bh.C keeps its SFPADDI and no SFPCONFIG appears.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fdump-tree-rvtt_combine" }
// { dg-final { scan-assembler "SFPADDI" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

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
