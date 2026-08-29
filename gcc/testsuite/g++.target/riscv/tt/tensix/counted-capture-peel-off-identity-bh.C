// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// Counted-capture peel flag-off identity control (lane IO): the fire
// twin's exact body WITHOUT -mtt-tensix-optimize-counted-capture-peel
// keeps today's plain counted-hoist benefit refusal byte-identically --
// no peel machinery runs, no replay words are emitted.
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -495 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "counted-capture-peel admitted" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "counted-capture-peel refused" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Counted-peel pricing" "rvtt_replay" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }
static volatile int sink;
void counted_peel_fire ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned i = 0; i != 8; ++i)
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
