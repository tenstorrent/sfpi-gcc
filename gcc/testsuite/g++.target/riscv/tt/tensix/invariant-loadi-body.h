void invariant_loadi ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      auto a = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      auto b = __builtin_rvtt_sfpxloadi (nullptr, 0xbf91c2e7, 0, 0, 31);
      x = __builtin_rvtt_sfpmad (x, a, b, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
