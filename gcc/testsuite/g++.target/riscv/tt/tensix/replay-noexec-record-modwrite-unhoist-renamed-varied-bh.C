// Lane FL (FH-1 genericity twin): renamed symbols, a different trip
// count, a different operation mix, and the OTHER audited mod-write
// class -- a typed Dst STORE through a non-no-increment address
// modifier (mod-2 auto-increment) as the last preheader word.  The
// placement obligation keys on the audited mod-write effect classes
// and the W_drain distance, never on names, trip counts, or the
// specific mod-write spelling.
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay" }
// { dg-final { scan-rtl-dump "Replay refusal: noexec-record-modwrite-window-unaudited" "rvtt_replay" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

void quarry_step_ladder ()
{
  auto p0 = __builtin_rvtt_sfpreadlreg (0);
  auto p1 = __builtin_rvtt_sfpreadlreg (1);
  auto p2 = __builtin_rvtt_sfpreadlreg (2);
  auto p3 = __builtin_rvtt_sfpreadlreg (3);
  auto q0 = __builtin_rvtt_sfpreadlreg (4);
  auto q1 = __builtin_rvtt_sfpreadlreg (5);
  auto q2 = __builtin_rvtt_sfpreadlreg (6);
  auto q3 = __builtin_rvtt_sfpreadlreg (7);
  __builtin_rvtt_sfpstore (nullptr, p0, 0, 0, 0, 8, 2);
  for (unsigned steps = 0; steps != 16; ++steps)
    {
      p0 = __builtin_rvtt_sfpadd (p0, q0, 0);
      p1 = __builtin_rvtt_sfpadd (p1, q1, 0);
      p2 = __builtin_rvtt_sfpadd (p2, q2, 0);
      p3 = __builtin_rvtt_sfpadd (p3, q3, 0);
      p0 = __builtin_rvtt_sfpmul (p0, q1, 0);
      p1 = __builtin_rvtt_sfpmul (p1, q2, 0);
      p2 = __builtin_rvtt_sfpmul (p2, q3, 0);
      p3 = __builtin_rvtt_sfpmul (p3, q0, 0);
      p0 = __builtin_rvtt_sfpadd (p0, q2, 0);
      p1 = __builtin_rvtt_sfpadd (p1, q3, 0);
      p2 = __builtin_rvtt_sfpadd (p2, q0, 0);
      p3 = __builtin_rvtt_sfpadd (p3, q1, 0);
    }
  __builtin_rvtt_sfpwritelreg (p0, 0);
  __builtin_rvtt_sfpwritelreg (p1, 1);
  __builtin_rvtt_sfpwritelreg (p2, 2);
  __builtin_rvtt_sfpwritelreg (p3, 3);
}
