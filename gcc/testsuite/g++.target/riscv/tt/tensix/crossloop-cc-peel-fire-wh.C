// Check the cross-loop CC-peel programming lift on Wormhole.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-crossloop-cc-peel -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "pressure-park: admitted post-CC candidate" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "cc-peel placement lifted to entry bb" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "peeled first iteration" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

void ccpeel_fire_wh (int tiles)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  for (int t = 0; t != tiles; ++t)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      for (int row = 0; row != 8; ++row)
	{
	  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 3);
	  __builtin_rvtt_sfppushc (0);
	  __builtin_rvtt_sfpsetcc (x, 0);
	  __builtin_rvtt_sfppopc (0);
	  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d,
						0, 0, 31);
	  x = __builtin_rvtt_sfpmul (x, gain, 0);
	  __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 3);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}
