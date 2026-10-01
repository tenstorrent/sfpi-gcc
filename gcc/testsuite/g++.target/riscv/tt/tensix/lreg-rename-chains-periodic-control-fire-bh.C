// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Periodic-window guard control twin: the SAME straight-line
// accumulate shape as lreg-rename-chains-periodic-refuse-bh.C, but the
// middle repetition multiplies x*x instead of x*k, so no two windows
// deliver the same word sequence -- there is no repeated window to
// protect, the guard stays silent, and the first storage-collision
// chain renames exactly as before the guard existed.
// { dg-final { scan-rtl-dump-not "regrename-periodic-window" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "Lreg chain rename: L\\d+ -> L\\d+" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "regrename-postcommit-divergence" "rvtt_lreg_rename_chains" } }
void renc_periodic_control ()
{
  auto k = __builtin_rvtt_sfpreadlreg (0);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  auto t1 = __builtin_rvtt_sfpmul (x, k, 0);
  x = __builtin_rvtt_sfpxor (x, t1);
  auto t2 = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpxor (x, t2);
  auto t3 = __builtin_rvtt_sfpmul (x, k, 0);
  x = __builtin_rvtt_sfpxor (x, t3);
  __builtin_rvtt_sfpwritelreg (x, 2);
}
