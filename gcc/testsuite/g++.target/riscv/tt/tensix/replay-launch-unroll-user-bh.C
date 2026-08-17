// A user-recorded capture launched from a proven-trip counted loop unrolls
// without any hoist involvement (and without a source pragma): the shape
// gate is {playback launches, counter step} + a provable trip count,
// however the loop came to exist.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Unrolled launch loop bb \\d+: 6 trips x 1 delivered words" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 7 } }
// { dg-final { scan-assembler-not "\\tbne\\t" } }

void user_launch_counted ()
{
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 1, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
  for (unsigned i = 0; i != 6; ++i)
    __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
}
