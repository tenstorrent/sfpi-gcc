/* A counted SFPU loop whose payload the counted-loop replay hoist captures
   into the preheader, leaving a body of pure replay delivery: one playback
   launch and one typed Dst step per trip.  The launch-loop unroll must then
   replicate that delivery back to back and remove the scalar loop control.
   The trip count is configurable so twins prove the decision keys only on
   the proven trip count and the delivered word count.  */

#ifndef LAUNCH_TRIP_COUNT
#define LAUNCH_TRIP_COUNT 20
#endif

void launch_loop_rows ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned row = 0; row != LAUNCH_TRIP_COUNT; ++row)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
