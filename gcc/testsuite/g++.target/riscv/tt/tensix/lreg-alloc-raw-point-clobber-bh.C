// Check that graph collection accepts a bare hard-LREG CLOBBER from a dead raw write while the opaque marker blocks spilling.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-lreg-alloc -mtt-tensix-dst-layout-32b -fdump-rtl-rvtt_lp_alloc-details -fdump-rtl-rvtt_lreg_livein" }
// { dg-final { scan-rtl-dump {\(clobber \(reg:XTT32SI 80 L0\)\)} "rvtt_lreg_livein" } }
// { dg-final { scan-rtl-dump "engaging DSATUR" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump-not "hard-sfpu-reg-pre-ira" "rvtt_lp_alloc" } }
// { dg-final { scan-rtl-dump "dst-rwc-effect-unproved" "rvtt_lp_alloc" } }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

void raw_point_clobber ()
{
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
  __builtin_rvtt_sfprawlreg_effect (0, 1);
}
