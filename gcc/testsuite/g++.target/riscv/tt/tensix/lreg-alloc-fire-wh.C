// Wormhole twin of lreg-alloc-fire-bh.C: nine loop-carried values
// compile under the DSATUR allocator; the WH no-increment address
// mode is 3 (BH is 7).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-lreg-alloc -mtt-tensix-dst-layout-32b -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "colorability=proven" "rvtt_lp_alloc" } }
// { dg-final { scan-assembler {\mSFPSTORE\tL[0-7], 252, 4, 3} } }
// { dg-final { scan-assembler {\mSFPLOAD\tL[0-7], 252, 4, 3} } }

void lreg_alloc_fire_wh (void)
{
  auto a0 = __builtin_rvtt_sfpreadlreg (0);
  auto a1 = __builtin_rvtt_sfpreadlreg (1);
  auto a2 = __builtin_rvtt_sfpreadlreg (2);
  auto a3 = __builtin_rvtt_sfpreadlreg (3);
  auto a4 = __builtin_rvtt_sfpreadlreg (4);
  auto a5 = __builtin_rvtt_sfpreadlreg (5);
  auto a6 = __builtin_rvtt_sfpreadlreg (6);
  auto a7 = __builtin_rvtt_sfpreadlreg (7);
  auto a8 = __builtin_rvtt_sfpmul (a0, a1, 0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto t = a0;
      a0 = __builtin_rvtt_sfpmad (a1, a2, a3, 0);
      a1 = __builtin_rvtt_sfpmad (a2, a3, a4, 0);
      a2 = __builtin_rvtt_sfpmad (a3, a4, a5, 0);
      a3 = __builtin_rvtt_sfpmad (a4, a5, a6, 0);
      a4 = __builtin_rvtt_sfpmad (a5, a6, a7, 0);
      a5 = __builtin_rvtt_sfpmad (a6, a7, a8, 0);
      a6 = __builtin_rvtt_sfpmad (a7, a8, t, 0);
      a7 = __builtin_rvtt_sfpmad (a8, t, a0, 0);
      a8 = __builtin_rvtt_sfpmad (t, a0, a1, 0);
    }
  __builtin_rvtt_sfpwritelreg (a0, 0);
  __builtin_rvtt_sfpwritelreg (a8, 1);
}
