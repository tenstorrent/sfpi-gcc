// Check that the allocator leaves a function whose eight live values fit the LREG file to IRA.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-lreg-alloc -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "peak pressure 8 within" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump "allocation left to IRA as today" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-not "engaging DSATUR" "rvtt_lp_alloc" } }
// { dg-final { scan-assembler-not {\mSFPSTORE\t} } }

void lreg_alloc_within (void)
{
  /* Establish both lane facts; ENCC alone does not clear ROW_MASK.  */
  __builtin_rvtt_sfpencc (10, 3);
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
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
