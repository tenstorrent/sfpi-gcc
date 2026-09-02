// OFF twin of crossloop-peel-anchor-general-refuse-bh.C: without
// -mtt-tensix-optimize-cc-region-general the placement walk stops at
// the tile loop by the standing name (its region scan refuses the
// crossed CC atoms), the lift never happens, and the new
// entry-anchored refusal has nothing to refuse -- the peel places at
// the inner loop's own entry exactly as before the R2 widening.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "crossloop-peel-entry-anchored" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "placement walk stops at loop bb \[0-9\]+ .crossloop-cc-unproven" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "peeled first iteration" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }

void ccpeel_lift_anchored_off (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    {
      auto scale = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
      for (int row = 0; row != 8; ++row)
	{
	  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	  __builtin_rvtt_sfppushc (0);
	  __builtin_rvtt_sfpsetcc_v (x, 0);
	  __builtin_rvtt_sfppopc (0);
	  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d,
						0, 0, 31);
	  x = __builtin_rvtt_sfpmul (x, gain, 0);
	  x = __builtin_rvtt_sfpmul (x, scale, 0);
	  __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}
