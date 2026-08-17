// The D2 path: a raw region containing an unauditable word (a MOP,
// which expands runtime-configured instructions) carries a typed
// effects declaration claiming SFPCONFIG destinations 12 and 13.  The
// freedom proof trusts the declaration, sees L12/L13 claimed, and
// allocates the remaining free register L14.  This is exactly the
// shape the exp kernel takes once the tt-metal header increment lands.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "prgm-const: allocated PRGM L14 for invariant immediate" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPCONFIG" } }

void declared_init ()
{
  __builtin_rvtt_ttregion_begin ((1u << 12) | (1u << 13), 0);
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x01800000));
  __builtin_rvtt_ttregion_end ();
}

void prgm_const_fire_after_declared_region ()
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
