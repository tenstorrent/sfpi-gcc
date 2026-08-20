// Raw-LREG reservation twin: a raw producer holds L7 (the ownership
// marker mints a livein sentinel interval ending in a bare USE), four
// more inputs are pinned by their reads, and four loop-carried
// computed values push the peak to nine.  The allocator must spill
// only unreserved webs -- the sentinel is a precolored node that
// constrains coloring, never a candidate -- and still prove
// colorability with the reservation held.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-lreg-alloc -mtt-tensix-dst-layout-32b -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "colorability=proven" "rvtt_lp_alloc" } }
// { dg-final { scan-assembler "# RAWLREG 0, 128" } }
// { dg-final { scan-assembler {\mSFPSTORE\tL[0-7], 252, 4, 7} } }

void lreg_alloc_reserved_sentinel (void)
{
  /* Raw code has filled L7; it stays reserved until read below.  */
  __builtin_rvtt_sfprawlreg_access (0, 0x80);
  auto a0 = __builtin_rvtt_sfpreadlreg (0);
  auto a1 = __builtin_rvtt_sfpreadlreg (1);
  auto a2 = __builtin_rvtt_sfpreadlreg (2);
  auto a3 = __builtin_rvtt_sfpreadlreg (3);
  auto b0 = __builtin_rvtt_sfpmul (a0, a1, 0);
  auto b1 = __builtin_rvtt_sfpmul (a1, a2, 0);
  auto b2 = __builtin_rvtt_sfpmul (a2, a3, 0);
  auto b3 = __builtin_rvtt_sfpmul (a3, a0, 0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      b0 = __builtin_rvtt_sfpmad (b1, a0, b2, 0);
      b1 = __builtin_rvtt_sfpmad (b2, a1, b3, 0);
      b2 = __builtin_rvtt_sfpmad (b3, a2, b0, 0);
      b3 = __builtin_rvtt_sfpmad (b0, a3, b1, 0);
    }
  auto raw = __builtin_rvtt_sfpreadlreg (7);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpmad (raw, b0, b3, 0), 0);
  __builtin_rvtt_sfpwritelreg (b2, 1);
}
