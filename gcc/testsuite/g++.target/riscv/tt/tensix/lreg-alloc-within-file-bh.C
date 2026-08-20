// No-op-below-the-wall control: eight live values fit the file, the
// allocator proves it trivially and changes NOTHING (flags-on
// byte-identity below the wall is corpus-gated; this is the dg-level
// witness).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-lreg-alloc -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "colorability=trivial" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-not "engaging DSATUR" "rvtt_lp_alloc" } }
// { dg-final { scan-assembler-not {\mSFPSTORE\t} } }

void lreg_alloc_within (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0000u, 0, 0, 31);
  auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0001u, 0, 0, 31);
  auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0002u, 0, 0, 31);
  auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0003u, 0, 0, 31);
  auto c4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0004u, 0, 0, 31);
  auto c5 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0005u, 0, 0, 31);
  auto c6 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0006u, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      x = __builtin_rvtt_sfpmad (x, c0, c1, 0);
      x = __builtin_rvtt_sfpmad (x, c2, c3, 0);
      x = __builtin_rvtt_sfpmad (x, c4, c5, 0);
      x = __builtin_rvtt_sfpmad (x, c6, c0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
