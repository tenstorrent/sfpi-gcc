/* Parameterized double-collision row for the GENERAL du-chain rename
   (item #7): a counted loop whose body materializes FOUR independent
   latency-bearing values that the allocator packs pairwise into two
   LREGs (first-fit reuse), each consumed through the non-tied XOR
   source position.  The writers carry a nonzero audited latency, so
   the v1 single-shape pass (latency-0 invariant-input members only)
   refuses every chain and the general pass renames two -- the
   multi-member two-chain fire class.  The RENC_* macros are free
   names/ops: the rename decision must be identical under renaming and
   operation variation (dataflow shape and audited typed effects
   only).  */
void RENC_FN ()
{
  auto RENC_K1 = __builtin_rvtt_sfpreadlreg (0);
  auto RENC_K2 = __builtin_rvtt_sfpreadlreg (1);
  auto RENC_X = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned RENC_ROW = 0; RENC_ROW != RENC_TRIPS; ++RENC_ROW)
    {
      auto RENC_T = __builtin_rvtt_sfpmul (RENC_K1, RENC_K2, 0);
      auto RENC_P = __builtin_rvtt_sfpmul (RENC_X, RENC_X, 0);
      auto RENC_R = RENC_CONSUME (RENC_P, RENC_T);
      auto RENC_U = __builtin_rvtt_sfpmul (RENC_K2, RENC_K1, 0);
      auto RENC_T2 = __builtin_rvtt_sfpmul (RENC_K2, RENC_K2, 0);
      auto RENC_R2 = RENC_CONSUME (RENC_R, RENC_U);
      auto RENC_U2 = __builtin_rvtt_sfpmul (RENC_K1, RENC_K1, 0);
      auto RENC_R3 = RENC_CONSUME (RENC_R2, RENC_T2);
      RENC_X = RENC_CONSUME (RENC_R3, RENC_U2);
    }
  __builtin_rvtt_sfpwritelreg (RENC_X, 2);
}
