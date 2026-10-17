// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Check that an opaque instruction anywhere in the function refuses every chain, since DF hard-register liveness is unreliable.
// { dg-final { scan-rtl-dump "Lreg chain rename refused: regrename-liveness-untrusted" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "Lreg chain rename: L" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "Lreg chain rename: renames=0" "rvtt_lreg_rename_chains" } }
void renc_opaque_liveness ()
{
  // An opaque word in the entry block makes DF liveness untrustworthy
  // without touching any chain span.
  asm volatile (".ttinsn %0" :: "n" (0x91000000u));
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto t = __builtin_rvtt_sfpmul (k1, k2, 0);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      auto r = __builtin_rvtt_sfpxor (p, t);
      auto u = __builtin_rvtt_sfpmul (k2, k1, 0);
      auto t2 = __builtin_rvtt_sfpmul (k2, k2, 0);
      auto r2 = __builtin_rvtt_sfpxor (r, u);
      auto u2 = __builtin_rvtt_sfpmul (k1, k1, 0);
      auto r3 = __builtin_rvtt_sfpxor (r2, t2);
      x = __builtin_rvtt_sfpxor (r3, u2);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}
