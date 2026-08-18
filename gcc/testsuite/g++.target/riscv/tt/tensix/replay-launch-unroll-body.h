/* A counted SFPU loop whose payload the counted-loop replay hoist captures
   into the preheader, leaving a body of pure replay delivery: one playback
   launch and one typed Dst step per trip.  The launch-loop unroll must then
   replicate that delivery back to back and remove the scalar loop control.
   Delivery-bound body (four rotating accumulators, zero modeled interlock
   stalls) so the corrected reissue model prices the hoist positive.  The
   trip count is configurable so twins prove the decision keys only on the
   proven trip count and the delivered word count.  */

#ifndef LAUNCH_TRIP_COUNT
#define LAUNCH_TRIP_COUNT 20
#endif

void launch_loop_rows ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned row = 0; row != LAUNCH_TRIP_COUNT; ++row)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}
