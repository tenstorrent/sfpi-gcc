/* A constant replay loop under "#pragma GCC unroll 1" and an unannotated copy.  */

void pinned_scalar_accumulate ()
{
  auto acc = __builtin_rvtt_sfpreadlreg (1);
#pragma GCC unroll 1
  for (unsigned remaining = 8; remaining != 0; --remaining)
    {
      auto k0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f100001, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k0, 0);
      auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f100002, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k1, 0);
      auto k2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f100003, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k2, 0);
      auto k3 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f100004, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k3, 0);
      auto k4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f100005, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k4, 0);
      auto k5 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f100006, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k5, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 1);
}

void unannotated_control ()
{
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned remaining = 8; remaining != 0; --remaining)
    {
      auto k0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f200001, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k0, 0);
      auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f200002, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k1, 0);
      auto k2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f200003, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k2, 0);
      auto k3 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f200004, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k3, 0);
      auto k4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f200005, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k4, 0);
      auto k5 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f200006, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k5, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
