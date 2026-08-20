// CC refusal twin: SFPSTORE/SFPLOAD move only CC-enabled lanes and no
// all-lanes store variant exists, so the spill round trip is complete
// only under provably all-lanes CC.  An integer add (whose mod field
// can architecturally set CC -- the conservative audited effect) makes
// the function CC-impure: the allocator refuses by the established
// name (cc-enable-unproved) and the named pressure error stays.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-lreg-alloc -fdump-rtl-rvtt_lp_alloc-details" }
// { dg-final { scan-rtl-dump "cc-enable-unproved" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-not "colorability=proven" "rvtt_lp_alloc" } }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

void lreg_alloc_cc_refuse (void)
{
  auto a0 = __builtin_rvtt_sfpreadlreg (0);
  auto a1 = __builtin_rvtt_sfpreadlreg (1);
  auto a2 = __builtin_rvtt_sfpreadlreg (2);
  auto a3 = __builtin_rvtt_sfpreadlreg (3);
  auto a4 = __builtin_rvtt_sfpreadlreg (4);
  auto a5 = __builtin_rvtt_sfpreadlreg (5);
  auto a6 = __builtin_rvtt_sfpreadlreg (6);
  auto a7 = __builtin_rvtt_sfpreadlreg (7);
  auto a8 = __builtin_rvtt_sfpiadd_v (a0, a1, 4);
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
