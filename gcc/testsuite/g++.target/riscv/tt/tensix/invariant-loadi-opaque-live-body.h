void opaque_live_across ()
{
  asm volatile (".ttinsn 0x71000000");
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000001, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  asm volatile (".ttinsn 0x72000000");
}
