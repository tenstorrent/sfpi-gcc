// Interfering-copy + web-class near-miss (FABLE_GOES_BURR.md item #6):
// the lreg-alloc-fire-bh.C loop body under the coalescing flag.  Its
// loop-rotation copy's halves genuinely interfere (the source is
// redefined while the dest lives across the backedge) -- different
// values, never a coalescing candidate (coalesce-interfering-copy) --
// and a later round's copy against a spill-generated reload temporary
// refuses by class (coalesce-web-class: merging would export never-
// spill to an ordinary web).  Spill decisions stay byte-identical to
// lreg-alloc-fire-bh.C, whose scans this twin repeats.
// TODAY: both refusals fire, 3 spills as at flag-off.
// FUTURE-VERDICT: interfering copies stay refusals forever (they are
// correctness, not conservatism).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-lreg-alloc -mtt-tensix-optimize-lreg-coalesce -mtt-tensix-dst-layout-32b -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "coalesce-refusal: coalesce-interfering-copy" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump "coalesce-refusal: coalesce-web-class" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-not "lreg-alloc coalesce: merged web" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-times "spilling web" 3 "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump "colorability=proven" "rvtt_lp_alloc" } }
// { dg-final { scan-assembler {\mSFPSTORE\tL[0-7], 252, 4, 7} } }
// { dg-final { scan-assembler {\mSFPLOAD\tL[0-7], 252, 4, 7} } }

void lreg_coalesce_interfering (void)
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
