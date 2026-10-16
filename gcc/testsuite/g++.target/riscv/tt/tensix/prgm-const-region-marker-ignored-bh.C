// Check that ttregion markers declare nothing: the MOP word is admitted by derivation and the marker mask claims no register.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "prgm-const: allocated PRGM L12 for invariant immediate" "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPMAD" } }

void formerly_declared_init ()
{
  __builtin_rvtt_ttregion_begin ((1u << 12) | (1u << 13), 0);
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x01800000));
  __builtin_rvtt_ttregion_end ();
}

void no_fire_after_marker_retirement ()
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
