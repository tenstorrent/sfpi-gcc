// PEEL-class placement under -mtt-tensix-optimize-cc-region-general: its
// all-lanes entry proof admits the crossed CC atoms of the tile loop, so
// the placement walk lifts the inner CC-canonical loop's entry to the
// tile loop's preheader -- but a PEEL-class candidate is entry-anchored
// (peel_first_iteration copies the loop body's first iteration onto the
// placement edge: a lifted peel would seed its header PHIs from a
// non-incident edge, move the copied body above the tile loop's own
// definitions -- here SCALE, the per-tile vector the copy consumes --
// and run once per function instead of once per tile).  The lift refuses
// by name and the candidate keeps the entry-anchored peel unchanged.
// The tile loop's partial-lane SFPENCC keeps the inner loop's entry
// state unproven, so its CC-canonical body still needs the peel.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "lift refused .crossloop-peel-entry-anchored" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "peeled first iteration" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x3e4b1a3d .loop class, 1 uses" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }

void ccpeel_lift_anchored (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    {
      /* Per-tile vector definition the inner body consumes: a lifted
	 peel copy would read it before it exists.  */
      auto scale = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
      __builtin_rvtt_sfpencc (10, 0);
      for (int row = 0; row != 8; ++row)
	{
	  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	  __builtin_rvtt_sfppushc (0);
	  __builtin_rvtt_sfpsetcc (x, 0);
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
