// Check that a non-plain SFPMAD mod (mad-mod-unproven) and a shared constant materialization are not allocated.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "prgm-const: sfpmad refused .mad-mod-unproven." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "prgm-const: allocated" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void mad_mod_refuses ()
{
  auto x = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto k = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb00001, 0, 0, 31);
      x = __builtin_rvtt_sfpmad (x, k, x, 2);
    }
  __builtin_rvtt_sfpwritelreg (x, 1);
}

void mad_shared_load_refuses ()
{
  auto x = __builtin_rvtt_sfpreadlreg (2);
  auto y = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto k = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb00002, 0, 0, 31);
      x = __builtin_rvtt_sfpmad (x, k, x, 0);
      y = __builtin_rvtt_sfpmad (y, k, y, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
  __builtin_rvtt_sfpwritelreg (y, 3);
}
