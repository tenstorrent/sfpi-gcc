// MAD-PAIR vocabulary flag-off control (lane HJ): the exact
// compl-wrapper and _lv fire shapes of the vocab fire twins, compiled
// WITHOUT -mtt-tensix-optimize-madpair-vocabulary.  The base discovery
// must keep its established recognition byte-identically: no madpair
// candidate, no madpair-class PRGM allocation, and the historical
// fold decay stands (per-iteration MULI in the compl shape).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "madpair loop bb \\d+ candidate" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .madpair class" "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPMULI" } }

void vocab_compl_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto twice = __builtin_rvtt_sfpxloadi (nullptr, 0x40000000, 0, 0, 31);
  auto lift = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, twice, 0);
      auto neg = __builtin_rvtt_sfpmov (prod, 1);
      x = __builtin_rvtt_sfpadd (lift, neg, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void vocab_lv_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (2);
  auto keep = __builtin_rvtt_sfpreadlreg (3);
  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3f100000, 0, 0, 31);
  auto lift = __builtin_rvtt_sfpxloadi (nullptr, 0x3f680000, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, gain, 0);
      x = __builtin_rvtt_sfpadd_lv (keep, prod, lift, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}
