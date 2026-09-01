// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -mtt-tensix-optimize-rename-temporal -fdump-rtl-rvtt_lreg_rename_chains-details" }
// R1 temporal tier EXHAUSTED: every spare architectural LREG is live
// AROUND the loop (read before it, written after it) -- live-in AND
// live-out of the row with no in-block touch, so neither the
// whole-block tier nor the temporal tier can admit it (a live-through
// register has no in-span-free lifetime story: no post-span fresh
// definition exists and DF liveness bars the dead-at-exit arm).  The
// wall refusal keeps its one name with the flag on.
// { dg-final { scan-rtl-dump "Lreg chain rename refused: regrename-no-free-lreg" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump "Lreg chain rename: renames=0" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "\\(temporal\\)" "rvtt_lreg_rename_chains" } }
void rent_exhausted ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto k3 = __builtin_rvtt_sfpreadlreg (3);
  auto k4 = __builtin_rvtt_sfpreadlreg (4);
  auto k5 = __builtin_rvtt_sfpreadlreg (5);
  auto k6 = __builtin_rvtt_sfpreadlreg (6);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto t = __builtin_rvtt_sfpmul (k1, k2, 0);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      auto r = __builtin_rvtt_sfpxor (p, t);
      auto u = __builtin_rvtt_sfpmul (k2, k1, 0);
      x = __builtin_rvtt_sfpxor (r, u);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
  __builtin_rvtt_sfpwritelreg (k3, 3);
  __builtin_rvtt_sfpwritelreg (k4, 4);
  __builtin_rvtt_sfpwritelreg (k5, 5);
  __builtin_rvtt_sfpwritelreg (k6, 6);
}
