// Near misses for the launch-loop unroll, all keeping their scalar loop:
// - a user playback loop with a runtime trip count refuses on the trip
//   proof (the refusal names the reason; no estimate ever substitutes);
// - a hoisted counted loop carrying a second scalar side effect beyond the
//   counter step refuses on the body shape;
// - a plain scalar counted loop is silently out of scope.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Launch-loop unroll refused: bb \\d+ trip count is not provably constant" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Launch-loop unroll refused: bb \\d+ has a second scalar insn" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Unrolled launch loop" "rvtt_replay" } }
// { dg-final { scan-assembler "\\tbne\\t" } }

void user_playback_runtime (unsigned n)
{
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 1, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
  for (unsigned i = 0; i != n; ++i)
    __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
}

unsigned extra_scalar_side_effect (unsigned seed)
{
  auto x = __builtin_rvtt_sfpreadlreg (1);
  unsigned acc = seed;
  for (unsigned row = 0; row != 12; ++row)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      acc = (acc << 1) ^ row;
    }
  __builtin_rvtt_sfpwritelreg (x, 1);
  return acc;
}

unsigned plain_scalar_loop (unsigned seed)
{
  unsigned acc = seed;
  for (unsigned i = 0; i != 24; ++i)
    acc = (acc << 1) ^ i;
  return acc;
}
