// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-lreg-rename-chains -fdump-rtl-rvtt_lreg_rename_chains-details" }
// Near miss per veto class: every colliding chain here is consumed
// through the destructive XOR's tied first source ("0"-matched to the
// destination) -- the read cannot split from the kill, the queued
// edits fail constraint re-recognition, and the group refuses with
// nothing changed.
// { dg-final { scan-rtl-dump "Lreg chain rename refused: regrename-constraint" "rvtt_lreg_rename_chains" } }
// { dg-final { scan-rtl-dump-not "Lreg chain rename: L" "rvtt_lreg_rename_chains" } }
void renc_tied ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto t = __builtin_rvtt_sfpmul (k1, k2, 0);
      auto u = __builtin_rvtt_sfpmul (k2, k1, 0);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      auto r = __builtin_rvtt_sfpxor (p, t);
      x = __builtin_rvtt_sfpxor (r, u);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}
