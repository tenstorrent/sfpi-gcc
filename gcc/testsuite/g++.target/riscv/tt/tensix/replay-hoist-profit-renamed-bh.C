// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Hoist profitable: modeled benefit 2241 >= 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Counted-loop replay payload" 1 "rvtt_replay" } }

// Renamed-equivalent of the fire witness: identical arithmetic under
// fresh names -- the decision is shape-derived, never name-derived.
void renamed_equivalent_row_engine ()
{
  auto north = __builtin_rvtt_sfpreadlreg (0);
  auto south = __builtin_rvtt_sfpreadlreg (1);
  auto east = __builtin_rvtt_sfpreadlreg (2);
  auto west = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned lap = 0; lap != 32; ++lap)
    {
      north = __builtin_rvtt_sfpmul (north, north, 0);
      south = __builtin_rvtt_sfpmul (south, south, 0);
      east = __builtin_rvtt_sfpmul (east, east, 0);
      west = __builtin_rvtt_sfpmul (west, west, 0);
      north = __builtin_rvtt_sfpmul (north, north, 0);
      south = __builtin_rvtt_sfpmul (south, south, 0);
      east = __builtin_rvtt_sfpmul (east, east, 0);
      west = __builtin_rvtt_sfpmul (west, west, 0);
    }
  __builtin_rvtt_sfpwritelreg (north, 0);
  __builtin_rvtt_sfpwritelreg (south, 1);
  __builtin_rvtt_sfpwritelreg (east, 2);
  __builtin_rvtt_sfpwritelreg (west, 3);
}
