// Check that two in-loop materializations of one value share one preheader PRGM programming.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "reused PRGM L1\\d for identical constant" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 4 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }

void same_value_pair (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto a = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
      auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, a, 0);
      x = __builtin_rvtt_sfpadd (x, b, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_varied_pair (void)
{
  auto west = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned step = 0; step != 12; ++step)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb, 0, 0, 31);
      auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb, 0, 0, 31);
      west = __builtin_rvtt_sfpmul (west, gain, 0);
      west = __builtin_rvtt_sfpadd (west, bias, 0);
    }
  __builtin_rvtt_sfpwritelreg (west, 2);
}

void distinct_values (void)
{
  auto y = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto a = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb504f3, 0, 0, 31);
      y = __builtin_rvtt_sfpmul (y, a, 0);
      y = __builtin_rvtt_sfpadd (y, b, 0);
    }
  __builtin_rvtt_sfpwritelreg (y, 1);
}
