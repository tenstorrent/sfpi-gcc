// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-latency-schedule -mtt-tensix-optimize-interlock-schedule -mtt-tensix-optimize-counted-row-formation -mtt-tensix-optimize-replay-loop-unroll -fdump-rtl-rvtt_replay" }
// Renamed/varied sibling of counted-row-vacated-shadow-refuse-bh.C: fewer
// trips, different coefficients, a shorter Horner chain, renamed values --
// the refusal keys on the vacated delay shadow, never on the identity of
// the tanh reproducer.
// { dg-final { scan-rtl-dump "counted-row-vacated-delay-shadow" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Canonicalized counted-row family" "rvtt_replay" } }

void shadow_varied_engine ()
{
  for (int i = 0; i < 24; ++i)
    {
      auto q  = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto b  = __builtin_rvtt_sfpabs (q, 1);
      auto k0 = __builtin_rvtt_sfploadi (nullptr, 0x412c, 0, 0, 0);
      auto u1 = __builtin_rvtt_sfpmad (b, k0, q, 0);
      auto u2 = __builtin_rvtt_sfpmad (u1, b, k0, 0);
      auto u3 = __builtin_rvtt_sfpmad (u2, b, u1, 0);
      auto u4 = __builtin_rvtt_sfpmad (u3, b, u2, 0);
      auto k1 = __builtin_rvtt_sfploadi (nullptr, 0x3d99, 0, 0, 0);
      auto u5 = __builtin_rvtt_sfpmad (u4, b, k1, 0);
      auto w  = __builtin_rvtt_sfpmul (u5, b, 0);
      auto lim = __builtin_rvtt_sfpreadlreg (10);
      auto mm = __builtin_rvtt_sfpswap (w, lim, 1);
      auto sel = __builtin_rvtt_sfpselect2 (mm, 0);
      __builtin_rvtt_sfpstore (nullptr, sel, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
