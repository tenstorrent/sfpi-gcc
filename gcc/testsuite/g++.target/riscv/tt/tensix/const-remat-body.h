// Nine loop-invariant fp32 constants live across a counted loop, more than the eight LREGs.

void NAME (void)
{
#ifdef CONST_RESIDENCY_ENTRY_PROLOGUE
  // Residency tests require actual full-lane programming, unlike remat alone.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
#endif
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto C (0) = __builtin_rvtt_sfpxloadi (nullptr, K (0), 0, 0, 31);
  auto C (1) = __builtin_rvtt_sfpxloadi (nullptr, K (1), 0, 0, 31);
  auto C (2) = __builtin_rvtt_sfpxloadi (nullptr, K (2), 0, 0, 31);
  auto C (3) = __builtin_rvtt_sfpxloadi (nullptr, K (3), 0, 0, 31);
  auto C (4) = __builtin_rvtt_sfpxloadi (nullptr, K (4), 0, 0, 31);
  auto C (5) = __builtin_rvtt_sfpxloadi (nullptr, K (5), 0, 0, 31);
  auto C (6) = __builtin_rvtt_sfpxloadi (nullptr, K (6), 0, 0, 31);
  auto C (7) = __builtin_rvtt_sfpxloadi (nullptr, K (7), 0, 0, 31);
  auto C (8) = __builtin_rvtt_sfpxloadi (nullptr, K (8), 0, 0, 31);
  for (unsigned ix = 0; ix != TRIPS; ++ix)
    {
      x = __builtin_rvtt_sfpmad (x, C (0), C (1), 0);
      x = __builtin_rvtt_sfpmad (x, C (2), C (3), 0);
      x = __builtin_rvtt_sfpmad (x, C (4), C (5), 0);
      x = __builtin_rvtt_sfpmad (x, C (6), C (7), 0);
      x = __builtin_rvtt_sfpmad (x, C (8), C (0), 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
