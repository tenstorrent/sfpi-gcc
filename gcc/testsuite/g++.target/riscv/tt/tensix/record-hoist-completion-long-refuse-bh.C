// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mno-tt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-completion-guard -fdump-rtl-rvtt_replay-details" }
// Drain-inclusive long-record refusal.  SFPARECIP is replay-safe but its RTL
// resource effects/result latency are not yet audited.  The completion guard
// must therefore retain the executing in-loop record instead of using the
// issue-only cancellation which would add a serialized playback.
// { dg-final { scan-rtl-dump "reissue-unproved: payload insn .* is effect-opaque" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Not hoisting: replay-reissue-latency-unproved: a consumed payload producer carries no audited result latency .loop \\d+, 32 words." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist refused: replay-reissue-latency-unproved" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 32, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 32, 0, 0" 1 } }

static volatile int long_record_sink;

#define LONG_STEP(V) a = __builtin_rvtt_sfpmuli (nullptr, a, V, 0, 0, 0)
#define LONG_BODY()                                                          \
  do                                                                         \
    {                                                                        \
      a = __builtin_rvtt_sfparecip (a, 0);                                   \
      LONG_STEP (0); LONG_STEP (1); LONG_STEP (2); LONG_STEP (3);            \
      LONG_STEP (4); LONG_STEP (5); LONG_STEP (6); LONG_STEP (7);            \
      LONG_STEP (8); LONG_STEP (9); LONG_STEP (10); LONG_STEP (11);          \
      LONG_STEP (12); LONG_STEP (13); LONG_STEP (14); LONG_STEP (15);        \
      LONG_STEP (16); LONG_STEP (17); LONG_STEP (18); LONG_STEP (19);        \
      LONG_STEP (20); LONG_STEP (21); LONG_STEP (22); LONG_STEP (23);        \
      LONG_STEP (24); LONG_STEP (25); LONG_STEP (26); LONG_STEP (27);        \
      LONG_STEP (28); LONG_STEP (29); LONG_STEP (30);                        \
    }                                                                        \
  while (0)

void completion_long_record_refuse ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      LONG_BODY ();
      long_record_sink = (int) ix;
      LONG_BODY ();
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
}

