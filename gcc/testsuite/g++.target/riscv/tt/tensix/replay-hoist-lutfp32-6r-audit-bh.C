// Check that a counted six-entry SFPLUTFP32 payload (no INDIRECT_VD) has audited effects and hoists as a replay.
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 2 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "effect-opaque" "rvtt_replay" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 8 } }

void lutfp32_6r_fire ()
{
  auto c0 = __builtin_rvtt_sfpreadlreg (0);
  auto c1 = __builtin_rvtt_sfpreadlreg (1);
  auto c2 = __builtin_rvtt_sfpreadlreg (2);
  auto c4 = __builtin_rvtt_sfpreadlreg (4);
  auto c5 = __builtin_rvtt_sfpreadlreg (5);
  auto c6 = __builtin_rvtt_sfpreadlreg (6);
  auto x = __builtin_rvtt_sfpreadlreg (3);
  auto d = __builtin_rvtt_sfpreadlreg (7);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      x = __builtin_rvtt_sfplutfp32_6r (c0, c1, c2, c4, c5, c6, x, 7);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      x = __builtin_rvtt_sfplutfp32_6r (c0, c1, c2, c4, c5, c6, x, 7);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      x = __builtin_rvtt_sfplutfp32_6r (c0, c1, c2, c4, c5, c6, x, 7);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      x = __builtin_rvtt_sfplutfp32_6r (c0, c1, c2, c4, c5, c6, x, 7);
      d = __builtin_rvtt_sfpmul (d, d, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 3);
  __builtin_rvtt_sfpwritelreg (d, 7);
}

void renamed_table1_varied ()
{
  auto k0 = __builtin_rvtt_sfpreadlreg (0);
  auto k1 = __builtin_rvtt_sfpreadlreg (1);
  auto k2 = __builtin_rvtt_sfpreadlreg (2);
  auto k4 = __builtin_rvtt_sfpreadlreg (4);
  auto k5 = __builtin_rvtt_sfpreadlreg (5);
  auto k6 = __builtin_rvtt_sfpreadlreg (6);
  auto y = __builtin_rvtt_sfpreadlreg (3);
  auto e = __builtin_rvtt_sfpreadlreg (7);
  for (unsigned trip = 0; trip != 24; ++trip)
    {
      y = __builtin_rvtt_sfplutfp32_6r (k0, k1, k2, k4, k5, k6, y, 2);
      e = __builtin_rvtt_sfpmul (e, e, 0);
      y = __builtin_rvtt_sfplutfp32_6r (k0, k1, k2, k4, k5, k6, y, 2);
      e = __builtin_rvtt_sfpmul (e, e, 0);
      y = __builtin_rvtt_sfplutfp32_6r (k0, k1, k2, k4, k5, k6, y, 2);
      e = __builtin_rvtt_sfpmul (e, e, 0);
      y = __builtin_rvtt_sfplutfp32_6r (k0, k1, k2, k4, k5, k6, y, 2);
      e = __builtin_rvtt_sfpmul (e, e, 0);
    }
  __builtin_rvtt_sfpwritelreg (y, 3);
  __builtin_rvtt_sfpwritelreg (e, 7);
}
