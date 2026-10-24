// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Check that the periodic-window guard refuses renaming one of several identical repetitions that replay could capture.
// { dg-final { scan-rtl-dump "Lreg chain rename refused: regrename-periodic-window" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "Lreg chain rename: renames=0" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "Lreg chain rename: L" "rvtt_lreg_rename_chains" } }
void renc_periodic ()
{
  auto k = __builtin_rvtt_sfpreadlreg (0);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  // Each repetition feeds the next through x, so no two temporaries
  // are common-subexpression-mergeable, yet post-RA every repetition
  // delivers the identical word pair (mul into the reused temporary
  // register, xor accumulate into x).
  auto t1 = __builtin_rvtt_sfpmul (x, k, 0);
  x = __builtin_rvtt_sfpxor (x, t1);
  auto t2 = __builtin_rvtt_sfpmul (x, k, 0);
  x = __builtin_rvtt_sfpxor (x, t2);
  auto t3 = __builtin_rvtt_sfpmul (x, k, 0);
  x = __builtin_rvtt_sfpxor (x, t3);
  __builtin_rvtt_sfpwritelreg (x, 2);
}
