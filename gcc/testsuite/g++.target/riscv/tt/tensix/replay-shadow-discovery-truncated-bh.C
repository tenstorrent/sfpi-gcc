// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-replay-shadow-discovery -fdump-rtl-rvtt_replay-details" }
// Suffix-automaton discovery, STAGE A -- the TRUNCATION class, which is
// the stage-B value this census exists to size.  A 40-word periodic run:
// the legacy discovery only ever grows to the replay buffer's own length
// (32 words on Blackhole), so the repeats that are LONGER than the buffer
// are invisible to it as maximal repeats -- the "we use all of a
// discovered sequence (or none of it) ... we could of course use the
// first N insns" limitation the pass admits at its head.  The automaton
// enumerates them and reports them by name; stage A still forms only the
// legacy pick.
// { dg-final { scan-rtl-dump "replay-maximal-repeats-truncated: bb \[0-9\]+: 5 maximal repeats longer than the 32-word buffer bound \\(longest 37\\)" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "superset OK \\(0 violations\\)" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "superset VIOLATED" "rvtt_replay" } }
// The picker is untouched: it forms the 6-word window it always did.
// { dg-final { scan-rtl-dump "Capturing and executing sequence \\\[0,6\\\) 6 instances to \\\[0,\\+6\\\) saving=24" "rvtt_replay" } }
void trunc_twin (unsigned n)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned ix = 0; ix != n; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
