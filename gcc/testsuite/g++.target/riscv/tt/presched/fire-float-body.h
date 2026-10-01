/* Float two-chain wide body (mul/add/mad, all audited mad-family
   latency-1): the xielu/lcm loss class -- loop-held operands and two
   value chains interleaved wide.  Ten live as written; eight after the
   pressure-cost schedule; no value is reassociated (the dataflow is
   untouched, only issue order moves).  */
void FIREF_NAME (void)
{
  auto a = __builtin_rvtt_sfpload (nullptr, FIREF_ROW (0), 0, 0, 3, FIREF_NOINC);
  auto b = __builtin_rvtt_sfpload (nullptr, FIREF_ROW (1), 0, 0, 3, FIREF_NOINC);
  auto c = __builtin_rvtt_sfpload (nullptr, FIREF_ROW (2), 0, 0, 3, FIREF_NOINC);
  auto d = __builtin_rvtt_sfpload (nullptr, FIREF_ROW (3), 0, 0, 3, FIREF_NOINC);
  auto e = __builtin_rvtt_sfpload (nullptr, FIREF_ROW (4), 0, 0, 3, FIREF_NOINC);
  auto u0 = __builtin_rvtt_sfpmul (a, b, 0);
  auto u1 = __builtin_rvtt_sfpmul (b, c, 0);
  auto u2 = __builtin_rvtt_sfpmul (c, d, 0);
  auto u3 = __builtin_rvtt_sfpmul (d, e, 0);
  auto u4 = __builtin_rvtt_sfpmul (e, a, 0);
  auto u5 = __builtin_rvtt_sfpadd (a, c, 0);
  auto u6 = __builtin_rvtt_sfpadd (b, d, 0);
  auto u7 = __builtin_rvtt_sfpadd (c, e, 0);
  auto r0 = __builtin_rvtt_sfpmad (u0, u1, u2, 0);
  auto r1 = __builtin_rvtt_sfpmad (u3, u4, u5, 0);
  auto r2 = __builtin_rvtt_sfpadd (u6, u7, 0);
  auto s0 = __builtin_rvtt_sfpmad (r0, r1, r2, 0);
  __builtin_rvtt_sfpstore (nullptr, s0, FIREF_OUT, 0, 0, 3, FIREF_NOINC);
}
