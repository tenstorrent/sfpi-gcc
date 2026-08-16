/* Renamed-equivalent, downward-counting variant of the replay-unroll loop
   with a configurable trip count.  The unroll request must key only on the
   proven constant trip count, not on names, direction, or coefficients.  */

#ifndef REPLAY_TRIP_COUNT
#define REPLAY_TRIP_COUNT 3
#endif

void scaled_accumulate ()
{
  auto acc = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned remaining = REPLAY_TRIP_COUNT; remaining != 0; --remaining)
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
