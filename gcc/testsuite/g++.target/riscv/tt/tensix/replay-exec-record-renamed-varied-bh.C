// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-replay-exec-record" }
// Renamed + constant-varied twin (down-counting loop, muli immediates):
// the exchange keys on the structural shape only.
// { dg-final { scan-assembler-times "TTREPLAY\t0, 8, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 8, 0, 0" 15 } }
void renamed_varied_columns ()
{
  auto north = __builtin_rvtt_sfpreadlreg (4);
  auto south = __builtin_rvtt_sfpreadlreg (5);
  auto east = __builtin_rvtt_sfpreadlreg (6);
  auto west = __builtin_rvtt_sfpreadlreg (7);
  for (unsigned lap = 16; lap != 0; --lap)
    {
      north = __builtin_rvtt_sfpmuli (nullptr, north, 0x3a11, 0, 0, 0);
      south = __builtin_rvtt_sfpmuli (nullptr, south, 0x3a21, 0, 0, 0);
      east = __builtin_rvtt_sfpmuli (nullptr, east, 0x3a31, 0, 0, 0);
      west = __builtin_rvtt_sfpmuli (nullptr, west, 0x3a41, 0, 0, 0);
      north = __builtin_rvtt_sfpmuli (nullptr, north, 0x3a51, 0, 0, 0);
      south = __builtin_rvtt_sfpmuli (nullptr, south, 0x3a61, 0, 0, 0);
      east = __builtin_rvtt_sfpmuli (nullptr, east, 0x3a71, 0, 0, 0);
      west = __builtin_rvtt_sfpmuli (nullptr, west, 0x3a81, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (north, 4);
  __builtin_rvtt_sfpwritelreg (south, 5);
  __builtin_rvtt_sfpwritelreg (east, 6);
  __builtin_rvtt_sfpwritelreg (west, 7);
}
