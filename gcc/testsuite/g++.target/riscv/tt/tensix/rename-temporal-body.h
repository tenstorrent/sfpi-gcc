/* Parameterized 8-LREG-wall row for the R1 temporal-target tier: a
   counted loop whose live set occupies every architectural LREG --
   two loop invariants, the loop-carried accumulator, two values live
   through the whole loop (read before, written after), and the
   packed short lifetimes of the row itself -- so the whole-block-free
   target search of the du-chain rename engine engine is exhausted.  One register's
   lifetimes are temporally disjoint from the colliding chain's span:
   the late pair (RENT_Y/RENT_Z) is materialized only AFTER the
   colliding chain closes, opening with a fresh all-write definition,
   which is exactly the temporal tier's admission shape.  Admission is
   no longer acceptance: under the strict-gain pricing the candidate
   must also buy modeled issue slots, which no temporal rename can
   (the engine's pricing note), so the flag-on twin now pins the
   regrename-temporal-no-modeled-gain refusal instead of a fire.  */
void RENT_FN ()
{
  auto k1 = __builtin_rvtt_sfpreadlreg (0);
  auto k2 = __builtin_rvtt_sfpreadlreg (1);
  auto k3 = __builtin_rvtt_sfpreadlreg (3);
  auto k4 = __builtin_rvtt_sfpreadlreg (4);
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned row = 0; row != 20; ++row)
    {
      /* The colliding front half: T and U pack into one LREG (the
	 allocator's first-fit reuse); U's chain is the rename root.  */
      auto t = __builtin_rvtt_sfpmul (k1, k2, 0);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      auto r = __builtin_rvtt_sfpxor (p, t);
      auto u = __builtin_rvtt_sfpmul (k2, k1, 0);
      auto s = __builtin_rvtt_sfpxor (r, u);
      auto w = __builtin_rvtt_sfpmul (k1, k1, 0);
      auto s2 = __builtin_rvtt_sfpxor (s, w);
      /* The late half: fresh lifetimes AFTER the front chains close --
	 the temporal target's out-of-span lifetime.  */
      auto y = __builtin_rvtt_sfpmul (k2, k2, 0);
      auto z = __builtin_rvtt_sfpmad (y, y, s2, 0);
      x = __builtin_rvtt_sfpxor (z, s2);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
  __builtin_rvtt_sfpwritelreg (k3, 3);
  __builtin_rvtt_sfpwritelreg (k4, 4);
}
