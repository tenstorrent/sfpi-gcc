// Check that a counted payload using the plain SFPGT/SFPLE compare form has audited effects and hoists as a replay.
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-not "effect-opaque" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "replay-reissue-latency-unproved" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 2 "rvtt_replay" } }
// { dg-final { scan-assembler-times "SFPGT\tL\\d+, L\\d+, 0, 1" 1 } }
// { dg-final { scan-assembler-times "SFPLE\tL\\d+, L\\d+, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY" 66 } }

void gt_clip_fire ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto zero = __builtin_rvtt_sfpreadlreg (9);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpgt (zero, a, 1);
      b = __builtin_rvtt_sfpassign_lv (b, a);
      __builtin_rvtt_sfppopc (0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
}

void renamed_le_floor ()
{
  auto north = __builtin_rvtt_sfpreadlreg (2);
  auto south = __builtin_rvtt_sfpreadlreg (3);
  auto floor = __builtin_rvtt_sfpreadlreg (9);
  for (unsigned step = 0; step != 32; ++step)
    {
      south = __builtin_rvtt_sfpmul (south, south, 0);
      north = __builtin_rvtt_sfpmul (north, north, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfple (floor, south, 1);
      north = __builtin_rvtt_sfpassign_lv (north, south);
      __builtin_rvtt_sfppopc (0);
      south = __builtin_rvtt_sfpmul (south, north, 0);
      north = __builtin_rvtt_sfpmul (north, north, 0);
    }
  __builtin_rvtt_sfpwritelreg (north, 2);
  __builtin_rvtt_sfpwritelreg (south, 3);
}
