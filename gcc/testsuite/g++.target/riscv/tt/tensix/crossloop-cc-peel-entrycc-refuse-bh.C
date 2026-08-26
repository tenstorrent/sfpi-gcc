// CROSSLOOP-CC-PEEL near miss, the CC proof genuinely failing (lane
// HR): a CC writer BEFORE the tile loop reaches the lifted preheader,
// so the fn-entry-all-lanes ambient the lifted programming needs is
// not on record -- the lift refuses by name and the established peel
// placement is kept byte-identically (the peel's own programming point
// follows the peeled trailing all-lanes SFPENCC, which is why the peel
// class never needed the ambient proof).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-crossloop-cc-peel -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "cc-peel lift refused .crossloop-cc-peel-entrycc-unproven" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "cc-peel placement lifted" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "peeled first iteration" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }

void ccpeel_entrycc_refuse (int tiles)
{
  /* A bare lane-enable rewrite ahead of the tile loop (the clean
     sets_cc witness; a raw sfpsetcc outside pushc/popc is a check-pass
     error).  The reach test is conservative over every CC writer --
     the value written does not matter, only that fn-entry state no
     longer provably reaches the lifted point.  */
  __builtin_rvtt_sfpencc (0, 10);
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
