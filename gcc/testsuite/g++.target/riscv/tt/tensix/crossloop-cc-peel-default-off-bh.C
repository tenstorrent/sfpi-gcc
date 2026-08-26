// CROSSLOOP-CC-PEEL default-off twin (lane HR): the exact
// crossloop-cc-peel-fire-bh.C primary body WITHOUT
// -mtt-tensix-optimize-crossloop-cc-peel.  The established behavior is
// kept byte-identically: the placement walk stops at the tile loop by
// the established name (its CC writers are not proven under the plain
// region discipline), the first iteration is peeled, and the
// programming follows the peeled all-lanes SFPENCC inside the tile
// loop.  Init(0): the flag never rides the ON set implicitly.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "placement walk stops at loop bb \\d+ .crossloop-cc-unproven." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "cc-peel" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "peeled first iteration" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }

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
