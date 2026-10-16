// Check that -mtt-tensix-optimize-madpair-vocabulary re-claims constants for a MAD pair whose add is an _lv merge.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-madpair-vocabulary -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "madpair loop bb \\d+ candidate: hoisted constant" 4 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .madpair class" 4 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "sfpmad" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPMAD" 2 } }
// { dg-final { scan-assembler-not "SFPADDI" } }
// { dg-final { scan-assembler-not "SFPMULI" } }

void vocab_lv_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto keep = __builtin_rvtt_sfpreadlreg (1);
  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3f100000, 0, 0, 31);
  auto lift = __builtin_rvtt_sfpxloadi (nullptr, 0x3f680000, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, gain, 0);
      x = __builtin_rvtt_sfpadd_lv (keep, prod, lift, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_masked_ramp (void)
{
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  auto park = __builtin_rvtt_sfpreadlreg (3);
  auto slope = __builtin_rvtt_sfpxloadi (nullptr, 0x3e900000, 0, 0, 31);
  auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x3f7a0000, 0, 0, 31);
  for (unsigned row = 0; row != 8; ++row)
    {
      auto scaled = __builtin_rvtt_sfpmul (acc, slope, 0);
      acc = __builtin_rvtt_sfpadd_lv (park, scaled, bias, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (acc, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
