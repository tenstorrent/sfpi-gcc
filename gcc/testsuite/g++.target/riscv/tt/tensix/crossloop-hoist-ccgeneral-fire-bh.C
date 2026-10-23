// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_crossloop" }
// Check that materializations lift across a tile loop carrying a balanced
// PUSHC/SETCC/POPC frame when the entry is not all-lanes.
// { dg-final { scan-tree-dump "CC activity tree-proven ambient-preserving" "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-times "crossloop-hoist: hoisted across loop" 2 "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "refused" "rvtt_crossloop" } }

extern volatile unsigned long __instrn_buffer[];

void
xlh_ccgeneral_fire (int tiles)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  /* A balanced frame is not itself a lane-state spoiler: POP restores
     the proven full state.  Establish partial flags explicitly below.  */
  {
    auto xs = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
    __builtin_rvtt_sfppushc (0);
    __builtin_rvtt_sfpsetcc (xs, 0);
    auto zs = __builtin_rvtt_sfpassign_lv (xs, xs);
    __builtin_rvtt_sfpstore (nullptr, zs, 0, 0, 0, 6, 7);
    __builtin_rvtt_sfppopc (0);
  }
  __builtin_rvtt_sfpencc (10, 0);
  for (int t = 0; t != tiles; ++t)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0xa2820010));
      __instrn_buffer[0] = 0xb2020000u | ((unsigned) t & 0x1ffu);
      /* The crossed balanced frame the tree proves.  */
      {
	auto xg = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	__builtin_rvtt_sfppushc (0);
	__builtin_rvtt_sfpsetcc (xg, 0);
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
