// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-capture-peel -fdump-rtl-rvtt_replay-details" }
// Counted-capture peel trips refusal: a runtime trip count
// refuses the peel by counted-capture-peel-trips-unproven (the counter
// re-init needs the provable_constant_trips chain; runtime-trip
// admission is deliberately not extended here) and keeps the rolled
// loop's bytes.
// { dg-final { scan-rtl-dump "counted-capture-peel refused: counted-capture-peel-trips-unproven" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "counted-capture-peel admitted" "rvtt_replay" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }
static volatile int sink;
void counted_peel_trips_refuse (unsigned n)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned i = 0; i != n; ++i)
    {
      auto t0 = __builtin_rvtt_sfpmul (a, b, 0);
      auto t1 = __builtin_rvtt_sfpmul (b, c, 0);
      auto t2 = __builtin_rvtt_sfpmul (c, a, 0);
      auto t3 = __builtin_rvtt_sfpmul (a, c, 0);
      auto t4 = __builtin_rvtt_sfpmul (b, a, 0);
      a = __builtin_rvtt_sfpmul (t0, t1, 0);
      b = __builtin_rvtt_sfpmul (t2, t3, 0);
      c = __builtin_rvtt_sfpmul (t4, t4, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
