// Renamed, constant-varied, down-counting twin without a Dst step: the
// unroll decision keys only on the proven trip count and the delivered
// word count, never on names, direction, coefficients, or a Dst step.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Counted-loop replay payload" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Unrolled launch loop bb \\d+: 15 trips x 1 delivered words" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 16 } }
// { dg-final { scan-assembler-not "\\tbne\\t" } }

void drain_scaled_columns ()
{
  auto acc = __builtin_rvtt_sfpreadlreg (3);
  auto vel = __builtin_rvtt_sfpreadlreg (4);
  auto pos = __builtin_rvtt_sfpreadlreg (5);
  auto err = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned remaining = 15; remaining != 0; --remaining)
    {
      acc = __builtin_rvtt_sfpmuli (nullptr, acc, 0x3f11, 0, 0, 0);
      vel = __builtin_rvtt_sfpmuli (nullptr, vel, 0x3f21, 0, 0, 0);
      pos = __builtin_rvtt_sfpmuli (nullptr, pos, 0x3f31, 0, 0, 0);
      err = __builtin_rvtt_sfpmuli (nullptr, err, 0x3f41, 0, 0, 0);
      acc = __builtin_rvtt_sfpmuli (nullptr, acc, 0x3f51, 0, 0, 0);
      vel = __builtin_rvtt_sfpmuli (nullptr, vel, 0x3f61, 0, 0, 0);
      pos = __builtin_rvtt_sfpmuli (nullptr, pos, 0x3f71, 0, 0, 0);
      err = __builtin_rvtt_sfpmuli (nullptr, err, 0x3f81, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 3);
  __builtin_rvtt_sfpwritelreg (vel, 4);
  __builtin_rvtt_sfpwritelreg (pos, 5);
  __builtin_rvtt_sfpwritelreg (err, 6);
}
