// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-replay-exec-record" }
// Renamed-equivalent + varied twin: different names, a different payload
// length (6) and a different trip count (4).
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 0" 3 } }
void rotated_polynomial_walk ()
{
  auto lane_state = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned step = 0; step != 4; ++step)
    {
      lane_state = __builtin_rvtt_sfpmul (lane_state, lane_state, 0);
      lane_state = __builtin_rvtt_sfpmul (lane_state, lane_state, 0);
      lane_state = __builtin_rvtt_sfpmul (lane_state, lane_state, 0);
      lane_state = __builtin_rvtt_sfpmul (lane_state, lane_state, 0);
      lane_state = __builtin_rvtt_sfpmul (lane_state, lane_state, 0);
      lane_state = __builtin_rvtt_sfpmul (lane_state, lane_state, 0);
    }
  __builtin_rvtt_sfpwritelreg (lane_state, 2);
}
