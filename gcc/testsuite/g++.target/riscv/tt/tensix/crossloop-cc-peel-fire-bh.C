// CROSSLOOP-CC-PEEL fire (lane HR): the CC-canonical peel class's
// programming lifts across the enclosing tile loop as a
// PROGRAMMING-ONLY placement.  The row loop's post-CC candidate (the
// pressure-park admission, consumer audited) would otherwise be
// programmed behind a first-iteration peel re-created on EVERY tile
// iteration -- the atan2 face-loop shape.  Under
// -mtt-tensix-optimize-crossloop-cc-peel the placement walk runs the
// cc-immaterial region discipline over the tile loop (its CC writers
// are structured typed atoms; the parked constant register is out of
// their reach), no function-local CC write reaches the lifted
// preheader, and the constants are programmed ONCE ahead of the tile
// loop with no peel at all.  The twin varies names, the constant, and
// both trip shapes: the decision must key on neither.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-crossloop-cc-peel -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "pressure-park: admitted post-CC candidate" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "cc-peel placement lifted to entry bb" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "peeled first iteration" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

void ccpeel_fire (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      for (int row = 0; row != 8; ++row)
	{
	  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	  __builtin_rvtt_sfppushc (0);
	  __builtin_rvtt_sfpsetcc_v (x, 0);
	  __builtin_rvtt_sfppopc (0);
	  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d,
						0, 0, 31);
	  x = __builtin_rvtt_sfpmul (x, gain, 0);
	  __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}

void renamed_varied_scale (int faces)
{
  for (int face = 0; face != faces; ++face)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      for (int lane = 0; lane != 12; ++lane)
	{
	  auto west = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	  __builtin_rvtt_sfppushc (0);
	  __builtin_rvtt_sfpsetcc_v (west, 0);
	  __builtin_rvtt_sfppopc (0);
	  auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb,
						0, 0, 31);
	  west = __builtin_rvtt_sfpmul (west, bias, 0);
	  __builtin_rvtt_sfpstore (nullptr, west, 0, 0, 0, 6, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}
