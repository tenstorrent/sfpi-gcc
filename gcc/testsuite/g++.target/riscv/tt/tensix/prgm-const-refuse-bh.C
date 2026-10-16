// Check the prgm-exhausted and cc-region-unproven refusals and a non-candidate SFPADDI in a TU without raw regions.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "prgm-const: refused .prgm-exhausted.:" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "prgm-const: loop bb \\d+ refused .cc-region-unproven.: a CC write reaches the programming point" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "prgm-const: allocated" "rvtt_prgm_const" } }

void user_claims_all_prgm ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  __builtin_rvtt_sfpwriteconfig_v (s, 0, 12);
  __builtin_rvtt_sfpwriteconfig_v (s, 0, 13);
  __builtin_rvtt_sfpwriteconfig_v (s, 0, 14);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void cc_writer_blocks ()
{
  auto x = __builtin_rvtt_sfpreadlreg (2);
  auto s = __builtin_rvtt_sfpreadlreg (3);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc (s, 0);
  __builtin_rvtt_sfppopc (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x3f81, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}

void not_fusion_class ()
{
  auto x = __builtin_rvtt_sfpreadlreg (4);
  for (unsigned ix = 0; ix != 32; ++ix)
    x = __builtin_rvtt_sfpaddi (nullptr, x, 0x3c00, 0, 0, 0);
  __builtin_rvtt_sfpwritelreg (x, 4);
}
