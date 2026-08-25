/* Generic legal four-word execution-bound re-record body.  Four SFPSWAP
   words each carry the architectural next-slot acceptance stall, for eight
   audited interlocked execution slots on both BH and WH.  Thus the
   delivery-only record-hoist model fires while the completion-accurate
   shared model charges the complete record and refuses.  */

static volatile int completion_execbound_sink;

void completion_execbound_model ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      auto p0 = __builtin_rvtt_sfpswap (a, b, 1);
      a = __builtin_rvtt_sfpselect2 (p0, 0);
      b = __builtin_rvtt_sfpselect2 (p0, 1);
      auto p1 = __builtin_rvtt_sfpswap (a, b, 2);
      a = __builtin_rvtt_sfpselect2 (p1, 0);
      b = __builtin_rvtt_sfpselect2 (p1, 1);
      auto p2 = __builtin_rvtt_sfpswap (a, b, 1);
      a = __builtin_rvtt_sfpselect2 (p2, 0);
      b = __builtin_rvtt_sfpselect2 (p2, 1);
      auto p3 = __builtin_rvtt_sfpswap (a, b, 2);
      a = __builtin_rvtt_sfpselect2 (p3, 0);
      b = __builtin_rvtt_sfpselect2 (p3, 1);
      completion_execbound_sink = (int) ix;
      auto q0 = __builtin_rvtt_sfpswap (a, b, 1);
      a = __builtin_rvtt_sfpselect2 (q0, 0);
      b = __builtin_rvtt_sfpselect2 (q0, 1);
      auto q1 = __builtin_rvtt_sfpswap (a, b, 2);
      a = __builtin_rvtt_sfpselect2 (q1, 0);
      b = __builtin_rvtt_sfpselect2 (q1, 1);
      auto q2 = __builtin_rvtt_sfpswap (a, b, 1);
      a = __builtin_rvtt_sfpselect2 (q2, 0);
      b = __builtin_rvtt_sfpselect2 (q2, 1);
      auto q3 = __builtin_rvtt_sfpswap (a, b, 2);
      a = __builtin_rvtt_sfpselect2 (q3, 0);
      b = __builtin_rvtt_sfpselect2 (q3, 1);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
}
