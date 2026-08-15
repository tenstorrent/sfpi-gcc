/* P1 reads P0, so it is not an independent ready filler.  */
void
latency_fill_raw_rejected ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  auto p0 = __builtin_rvtt_sfpmul (a, b, 0);
  auto r0 = __builtin_rvtt_sfpmad (p0, c, d, 0);
  auto p1 = __builtin_rvtt_sfpmul (p0, b, 0);
  __builtin_rvtt_sfpwritelreg (r0, 0);
  __builtin_rvtt_sfpwritelreg (p1, 1);
}

/* A CC-mutating instruction is an explicit scheduling barrier.  */
void
latency_fill_cc_rejected ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  auto p0 = __builtin_rvtt_sfpmul (a, b, 0);
  auto r0 = __builtin_rvtt_sfpmad (p0, c, d, 0);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (b, 0);
  __builtin_rvtt_sfppopc (0);
  auto p1 = __builtin_rvtt_sfpmul (c, d, 0);
  __builtin_rvtt_sfpwritelreg (r0, 0);
  __builtin_rvtt_sfpwritelreg (p1, 1);
}
