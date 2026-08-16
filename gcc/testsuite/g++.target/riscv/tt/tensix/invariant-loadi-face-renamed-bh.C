// Renamed-equivalent copy of the typed face-loop shape: identical semantics
// under unrelated names must make identical decisions.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 4 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "function has opaque LREG state" "rvtt_invariant" } }

void completely_unrelated_stage_walker ()
{
  auto accumulator = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned outer_stage = 0; outer_stage != 4; ++outer_stage)
    {
      for (unsigned lane_pass = 0; lane_pass != 8; ++lane_pass)
	{
	  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
	  auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0xbf91c2e7, 0, 0, 31);
	  accumulator = __builtin_rvtt_sfpmad (accumulator, gain, bias, 0);
	}
      __builtin_rvtt_ttdstface ();
    }
  __builtin_rvtt_sfpwritelreg (accumulator, 0);
}
