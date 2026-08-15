void invariant_pressure ()
{
#ifdef LIVE_ACROSS_LOOP
  auto preserved = __builtin_rvtt_sfpreadlreg (7);
#endif
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000001, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c0, 0);
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
#ifdef NINE_LREG_PRESSURE
      auto c6 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000007, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c6, 0);
#endif
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
#ifdef LIVE_ACROSS_LOOP
  __builtin_rvtt_sfpwritelreg (preserved, 7);
#endif
}
