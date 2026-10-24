// Check that the widened MAD-pair discovery skips non-FLOATB and multi-use wrappers and refuses a shared constant.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-madpair-vocabulary -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "refused .madpair-shared-constant" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "madpair loop bb \\d+ candidate" "rvtt_prgm_const" } }

void nonvulnerable_wrapper (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto third = __builtin_rvtt_sfpxloadi (nullptr, 0x3e2aaaab, 0, 0, 31);
  auto lift = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, third, 0);
      auto neg = __builtin_rvtt_sfpmov (prod, 1);
      x = __builtin_rvtt_sfpadd (lift, neg, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void multiuse_wrapper (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  auto twice = __builtin_rvtt_sfpxloadi (nullptr, 0x40400000, 0, 0, 31);
  auto lift = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, twice, 0);
      auto neg = __builtin_rvtt_sfpmov (prod, 1);
      auto sum = __builtin_rvtt_sfpadd (lift, neg, 0);
      x = __builtin_rvtt_sfpmul (sum, neg, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}

void shared_vulnerable (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (4);
  auto keep = __builtin_rvtt_sfpreadlreg (5);
  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3f100000, 0, 0, 31);
  auto lift = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, gain, 0);
      auto other = __builtin_rvtt_sfpadd (lift, gain, 0);
      x = __builtin_rvtt_sfpadd_lv (keep, prod, other, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 4);
}
