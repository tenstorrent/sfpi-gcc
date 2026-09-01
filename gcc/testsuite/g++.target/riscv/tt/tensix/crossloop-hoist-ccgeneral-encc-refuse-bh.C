// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_crossloop" }
// R2 crossloop near miss: a NON-all-lanes SFPENCC in the crossed tile
// loop can widen the lane-enable state beyond the lifted entry's,
// AND the entry is not provably all-lanes (a preceding structured
// region spoils the backward kill proof) -- neither tree fact holds,
// so the walk stops by the widening's OWN name and the
// materializations stay in the row preheader.
// { dg-final { scan-tree-dump "crossloop-cc-ambient-unproven" "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "hoisted across" "rvtt_crossloop" } }

extern volatile unsigned int __instrn_buffer[];

void
xlh_ccgeneral_encc (int tiles)
{
  /* Entry-state spoiler (see the fire twin).  */
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
      __builtin_rvtt_sfpencc (0, 10);
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
