// Pre-peel ambient near miss: an UNKILLED CC write reaches
// the pre-peel point -- the outer loop rewrites the lane state with a
// non-all-lanes SFPENCC before the canonical loop and never restores
// all lanes on that path -- so park-prepeel-ambient-unproven refuses by
// name and the park LREG tier keeps the established post-peel
// programming-point placement byte-identically (the peel duplicate
// stays: it is what reproduces iteration one under the narrowed
// ambient).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "park-prepeel-ambient-unproven" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "pressure-park: hoisted invariant materialization to a free LREG at the programming point .preheader bb \\d+." 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "pre-peel entry" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 3 } }

void ambient_refuse (int faces)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (int face = 0; face != faces; ++face)
    {
      /* Ambient lane-state rewrite on the path into the canonical
	 loop: a lane-clearing (non-all-lanes-word) SFPENCC with no
	 all-lanes SFPENCC between it and the loop entry.  */
      __builtin_rvtt_sfpencc (8, 0);
      for (unsigned ix = 0; ix != 32; ++ix)
	{
	  __builtin_rvtt_sfppushc (0);
	  __builtin_rvtt_sfpsetcc_v (x, 0);
	  __builtin_rvtt_sfppopc (0);
	  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e2f107b, 0, 0, 31);
	  auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f0a3d29, 0, 0, 31);
	  auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x404149ea, 0, 0, 31);
	  auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x419923ca, 0, 0, 31);
	  x = __builtin_rvtt_sfpmul (x, c0, 0);
	  x = __builtin_rvtt_sfpmul (x, c1, 0);
	  x = __builtin_rvtt_sfpmul (x, c2, 0);
	  x = __builtin_rvtt_sfpmul (x, c3, 0);
	}
      __builtin_rvtt_sfpwritelreg (x, 1);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
