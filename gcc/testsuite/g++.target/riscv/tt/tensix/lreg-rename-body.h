/* Parameterized storage-collision row: a counted loop whose body
   materializes an invariant-input value, runs a latency-bearing chain,
   consumes the value, and materializes a SECOND independent value that
   the allocator packs into the same LREG (first-fit reuse).  The
   REN_* macros are free names/ops: the rename decision must be
   identical under renaming and operation variation (dataflow shape and
   audited effects only).  */
void REN_FN ()
{
  auto REN_K1 = __builtin_rvtt_sfpreadlreg (0);
  auto REN_K2 = __builtin_rvtt_sfpreadlreg (1);
  auto REN_X = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned REN_ROW = 0; REN_ROW != REN_TRIPS; ++REN_ROW)
    {
      auto REN_T = __builtin_rvtt_sfpand (REN_K1, REN_K2);
      auto REN_P = __builtin_rvtt_sfpmul (REN_X, REN_X, 0);
      auto REN_Q = __builtin_rvtt_sfpmul (REN_P, REN_P, 0);
      auto REN_R = REN_CONSUME (REN_Q, REN_T);
      auto REN_U = __builtin_rvtt_sfpand (REN_K2, REN_K1);
      REN_X = REN_CONSUME (REN_R, REN_U);
    }
  __builtin_rvtt_sfpwritelreg (REN_X, 2);
}
