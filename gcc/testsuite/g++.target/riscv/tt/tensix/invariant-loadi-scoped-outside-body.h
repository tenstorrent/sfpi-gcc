/* An inner constant loop with opaque asm before, around and after it.  */
void nested_shared_entry ()
{
  asm volatile (".ttinsn 0x71000000");
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned face = 0; face != 4; ++face)
    {
      for (unsigned ix = 0; ix != 8; ++ix)
	{
	  auto a = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
	  auto b = __builtin_rvtt_sfpxloadi (nullptr, 0xbf91c2e7, 0, 0, 31);
	  x = __builtin_rvtt_sfpmad (x, a, b, 0);
	}
      asm volatile (".ttinsn 0x72000000");
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  asm volatile (".ttinsn 0x73000000");
}
