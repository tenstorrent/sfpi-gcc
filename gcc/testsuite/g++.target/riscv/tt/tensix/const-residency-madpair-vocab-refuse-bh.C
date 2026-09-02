// MAD-PAIR vocabulary near-misses and named refusal.  Three
// twins under the widened discovery:
//   1. nonvulnerable_wrapper: the compl-wrapped pair's constant is a
//      full-image chain form (low bits set, not FLOATB-shortenable) --
//      no immediate fold matches it, the mad rule fuses register
//      operands where they are, and the widened discovery must claim
//      nothing (no candidate; GA's non-vulnerable discipline holds
//      through the new vocabulary).
//   2. multiuse_wrapper: the complement mov has a second consumer --
//      the wrapper is not the add's single feed and the mad rewrite
//      keeps it alive; skip (no candidate).
//   3. shared_vulnerable: the widened _lv discovery reaches a pair
//      whose fold-vulnerable constant has a consumer beyond the pair;
//      the established madpair-shared-constant refusal must fire
//      through the new vocabulary.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-madpair-vocabulary -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "refused .madpair-shared-constant" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "madpair loop bb \\d+ candidate" "rvtt_prgm_const" } }

void nonvulnerable_wrapper (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto third = __builtin_rvtt_sfpxloadi (nullptr, 0x3e2aaaab, 0, 0, 31);
  auto lift = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, third, 0);
      auto neg = __builtin_rvtt_sfpmov (prod, 1);
      x = __builtin_rvtt_sfpadd (lift, neg, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void multiuse_wrapper (void)
{
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
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}

void shared_vulnerable (void)
{
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
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 4);
}
