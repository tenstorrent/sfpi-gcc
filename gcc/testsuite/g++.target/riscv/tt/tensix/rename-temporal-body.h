/* A counted loop whose live set occupies every LREG, with one late pair of fresh lifetimes temporally disjoint from the colliding chain's span.  */
void RENT_FN ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto k3 = __builtin_rvtt_sfpreadlreg (3);
  auto k4 = __builtin_rvtt_sfpreadlreg (4);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned row = 0; row != 20; ++row)
    {
      /* T and U pack into one LREG; U's chain is the rename root.  */
      auto t = __builtin_rvtt_sfpmul (k1, k2, 0);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      auto r = __builtin_rvtt_sfpxor (p, t);
      auto u = __builtin_rvtt_sfpmul (k2, k1, 0);
      auto s = __builtin_rvtt_sfpxor (r, u);
      auto w = __builtin_rvtt_sfpmul (k1, k1, 0);
      auto s2 = __builtin_rvtt_sfpxor (s, w);
      /* Fresh lifetimes after the front chains close: the temporal
	 target's out-of-span lifetime.  */
      auto y = __builtin_rvtt_sfpmul (k2, k2, 0);
      auto z = __builtin_rvtt_sfpmad (y, y, s2, 0);
      x = __builtin_rvtt_sfpxor (z, s2);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
  __builtin_rvtt_sfpwritelreg (k3, 3);
  __builtin_rvtt_sfpwritelreg (k4, 4);
}
