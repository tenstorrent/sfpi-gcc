// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-capture-peel -fdump-rtl-rvtt_replay-details" }
// Counted-capture peel RENAMED-EQUIVALENT / VARIED-CONSTANTS adversary
// twin (renamed-twin recipe): different function and value names,
// different LREGs (4/5/6), a DIFFERENT trip count (6) and a longer
// mixed mul/add 9-word body.  The peel admission must key on the
// structural facts (counted single-block SFPU row loop whose plain
// counted hoist refuses on modeled benefit while the peeled shape
// prices positive), never on the original twin's identifiers or
// constants: the peel must still fire, trips 6 -> 5.
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -\\d+ < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "counted-capture-peel admitted: counted-loop bb \\d+ peeled exec-while-record \\(trips 6 -> 5\\)" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 9, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 9, 0, 0" 5 } }
static volatile int drainpipe;
void audit_ip_capture_body ()
{
  auto p = __builtin_rvtt_sfpreadlreg (4);
  auto q = __builtin_rvtt_sfpreadlreg (5);
  auto r = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned lap = 0; lap != 6; ++lap)
    {
      auto m0 = __builtin_rvtt_sfpmul (p, q, 0);
      auto m1 = __builtin_rvtt_sfpadd (q, r, 0);
      auto m2 = __builtin_rvtt_sfpmul (r, p, 0);
      auto m3 = __builtin_rvtt_sfpadd (p, r, 0);
      auto m4 = __builtin_rvtt_sfpmul (q, p, 0);
      auto m5 = __builtin_rvtt_sfpadd (m0, m2, 0);
      p = __builtin_rvtt_sfpmul (m0, m1, 0);
      q = __builtin_rvtt_sfpadd (m2, m3, 0);
      r = __builtin_rvtt_sfpmul (m4, m5, 0);
    }
  __builtin_rvtt_sfpwritelreg (p, 4);
  __builtin_rvtt_sfpwritelreg (q, 5);
  __builtin_rvtt_sfpwritelreg (r, 6);
}
