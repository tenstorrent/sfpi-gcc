using vec_t = __xtt_vector;

void invariant_internal_liveout ()
{
  vec_t saved;
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000001, 0, 0, 31);
      saved = __builtin_rvtt_sfpmul (x, c0, 0);
      x = __builtin_rvtt_sfpmul (saved, c0, 0);
      auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000002, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c1, 0);
      auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000003, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c2, 0);
      auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000004, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c3, 0);
      auto c4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000005, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c4, 0);
      auto c5 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000006, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c5, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpwritelreg (saved, 7);
}
