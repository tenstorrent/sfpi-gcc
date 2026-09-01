// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_crossloop" }
// THE R2 CROSSLOOP TREE-FACT FIRE (FABLE_GOES_BURR R2; the
// crossloop-cc-unproven widening, balanced-frame arm): the tile loop
// carries a BALANCED structured CC frame (plain PUSHC, an audited
// narrowing SETCC refinement, plain POPC).  A preceding structured
// region makes the lifted entry NOT provably all-lanes, so the
// admission must come from the loop fact itself: the CC-region tree
// proves the loop's CC activity ambient-preserving-and-narrowing (the
// popc restores the saved enable state) and both row-invariant
// materializations lift across the tile loop.
// { dg-final { scan-tree-dump "CC activity tree-proven ambient-preserving" "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-times "crossloop-hoist: hoisted across loop" 2 "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "refused" "rvtt_crossloop" } }

extern volatile unsigned int __instrn_buffer[];

void
xlh_ccgeneral_fire (int tiles)
{
  /* Entry-state spoiler: a balanced frame BEFORE the loops leaves the
     backward all-lanes proof unproven (its block's last CC event is a
     popc, not an all-lanes reset).  */
  {
    auto xs = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
    __builtin_rvtt_sfppushc (0);
    __builtin_rvtt_sfpsetcc_v (xs, 0);
    auto zs = __builtin_rvtt_sfpassign_lv (xs, xs);
    __builtin_rvtt_sfpstore (nullptr, zs, 0, 0, 0, 6, 7);
    __builtin_rvtt_sfppopc (0);
  }
  for (int t = 0; t != tiles; ++t)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0xa2820010));
      __instrn_buffer[0] = 0xb2020000u | ((unsigned) t & 0x1ffu);
      /* The crossed balanced frame the tree proves.  */
      {
	auto xg = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	__builtin_rvtt_sfppushc (0);
	__builtin_rvtt_sfpsetcc_v (xg, 0);
	auto zg = __builtin_rvtt_sfpassign_lv (xg, xg);
	__builtin_rvtt_sfpstore (nullptr, zg, 0, 0, 0, 6, 7);
	__builtin_rvtt_sfppopc (0);
      }
      for (int row = 0; row != 8; ++row)
	{
	  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, -32);
	  auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f2e8ba3, 0, 0, -32);
	  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	  x = __builtin_rvtt_sfpmad (x, c0, c1, 0);
	  __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}
