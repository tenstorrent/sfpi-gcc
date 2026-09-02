// Near misses for the programmable-constant allocation, all in a TU
// with no raw regions (the freedom proof itself passes):
// 1. every PRGM register already claimed by user vConstFloatPrgm
//    assignments (sfpwriteconfig_v): prgm-exhausted;
// 2. a CC-writing statement that can execute before the programming
//    point (here it precedes the loop) defeats the all-lanes proof:
//    cc-region-unproven (reach-scoped per the widened admission);
// 3. an SFPADDI whose operand is not a single-use SFPMUL is not in the
//    admitted fusion class: no candidate, bytes untouched.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "prgm-const: refused .prgm-exhausted.:" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "prgm-const: loop bb \\d+ refused .cc-region-unproven.: a CC write reaches the programming point" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "prgm-const: allocated" "rvtt_prgm_const" } }

void user_claims_all_prgm ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  __builtin_rvtt_sfpwriteconfig_v (s, 12);
  __builtin_rvtt_sfpwriteconfig_v (s, 13);
  __builtin_rvtt_sfpwriteconfig_v (s, 14);
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
  __builtin_rvtt_sfpsetcc_v (s, 0);
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
