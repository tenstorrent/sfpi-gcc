// Nine LOOP-CARRIED computed values (nothing to rematerialize) exceed
// the eight-LREG file: the shape that is a hard lreg-pressure-exceeded
// error by default (spill-diag-named-error-bh.C).  Under the DSATUR
// allocator the graph is spilled through a proven-free 32-bit Dst
// scratch row and the function COMPILES.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-lreg-alloc -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "engaging DSATUR" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump "colorability=proven" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump "spilling web" "rvtt_lp_alloc" } }
// { dg-final { scan-assembler {\mSFPSTORE\tL[0-7], 252, 4, 7} } }
// { dg-final { scan-assembler {\mSFPLOAD\tL[0-7], 252, 4, 7} } }

void lreg_alloc_fire (void)
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
      /* Every value is both consumed and redefined per iteration: all
	 nine stay live across the backedge.  */
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
