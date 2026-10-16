/* A constant eight-trip loop with opaque asm outside the hoist region.  */
void scoped_unroll ()
{
  asm volatile (".ttinsn 0x76000000");
  auto acc = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned remaining = 8; remaining != 0; --remaining)
    {
      auto k0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f100001, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k0, 0);
      auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f100002, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k1, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 1);
  asm volatile (".ttinsn 0x77000000");
}
