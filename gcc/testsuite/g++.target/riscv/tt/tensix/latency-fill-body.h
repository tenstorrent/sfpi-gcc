/* Two independent SFPU chains in deliberately serialized source order.  The
   opt-in latency pass may move the second multiply between the first multiply
   and its dependent MAD.  The writes keep both chains live.  */
void
latency_fill_two_chains ()
{
  auto a0 = __builtin_rvtt_sfpreadlreg (0);
  auto c0 = __builtin_rvtt_sfpreadlreg (2);
  auto d0 = __builtin_rvtt_sfpreadlreg (3);
  auto a1 = __builtin_rvtt_sfpreadlreg (4);
  auto c1 = __builtin_rvtt_sfpreadlreg (6);
  auto d1 = __builtin_rvtt_sfpreadlreg (7);

  auto p0 = __builtin_rvtt_sfpmuli (nullptr, a0, 0, 0, 0, 0);
  auto r0 = __builtin_rvtt_sfpmad (p0, c0, d0, 0);
  auto p1 = __builtin_rvtt_sfpmuli (nullptr, a1, 0, 0, 0, 0);
  auto r1 = __builtin_rvtt_sfpmad (p1, c1, d1, 0);
  __builtin_rvtt_sfpwritelreg (r0, 0);
  __builtin_rvtt_sfpwritelreg (r1, 1);
}
