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
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned row = 0; row != 200; ++row)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
