// Size near miss: 200 proven trips of a one-word launch body exceed the
// cost-table straight-line bound (XTT_REPLAY_LAUNCH_UNROLL_MAX_WORDS), so
// the hoist fires but the unroll refuses and the scalar loop survives.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Launch-loop unroll refused: bb \\d+ unrolled size 200 words exceeds 128" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Unrolled launch loop" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 2 } }
// { dg-final { scan-assembler "\\tbne\\t" } }

void two_hundred_rows ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned row = 0; row != 200; ++row)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}
