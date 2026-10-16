// Check that a PEEL-class candidate refuses to lift its entry-anchored peel above
// the tile loop under -mtt-tensix-optimize-cc-region-general.
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
