// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }

/* A raw LLK producer has filled L0--L3 before this SFPI region.  L1--L3 are
   deliberately consumed only after temporaries made from L0.  The metadata
   marker must make IRA keep every temporary off each future input.  */
void
raw_lreg_future_input ()
{
#ifndef OMIT_RAW_LREG_MARKER
  __builtin_rvtt_sfprawlreg_access (0, 0x0f);
#endif
  auto l0 = __builtin_rvtt_sfpreadlreg (0);
  auto mean = __builtin_rvtt_sfpreadlreg (4);
  auto m2 = __builtin_rvtt_sfpreadlreg (5);
  auto delta = __builtin_rvtt_sfpmad (l0, l0, mean, 0);
  auto tmp = __builtin_rvtt_sfpmad (delta, l0, mean, 0);
  auto next_m2 = __builtin_rvtt_sfpmad (m2, delta, tmp, 0);
  __builtin_rvtt_sfpwritelreg (next_m2, 4);
  auto l1 = __builtin_rvtt_sfpreadlreg (1);
  auto l2 = __builtin_rvtt_sfpreadlreg (2);
  auto l3 = __builtin_rvtt_sfpreadlreg (3);
  auto result = __builtin_rvtt_sfpmad (l1, l2, l3, 0);
  __builtin_rvtt_sfpwritelreg (result, 5);
}

// { dg-final { scan-assembler "# RAWLREG 0, 15" } }
// { dg-final { scan-assembler "SFPMAD\\tL[6-7], L0, L0, L4, 0" } }
// { dg-final { scan-assembler-not "SFPMAD\\tL1, L0, L0, L4, 0" } }
// { dg-final { scan-assembler-not "SFPMAD\\tL2, L0, L0, L4, 0" } }
// { dg-final { scan-assembler-not "SFPMAD\\tL3, L0, L0, L4, 0" } }
