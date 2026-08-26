// CROSSLOOP-CC-PEEL near miss, unrelated shape 1 (lane HR): a REPLAY
// word in the tile loop.  The cc-immaterial discipline relaxes ONLY
// the typed structured-CC-atom refusal -- recorded replay content is
// still not derivable, the walk stops by the established name, and the
// peel placement is kept byte-identically.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-crossloop-cc-peel -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "placement walk stops at loop bb \\d+ .crossloop-replay-unproven." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "cc-peel placement lifted" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "peeled first iteration" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }

void ccpeel_replay_refuse (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    {
      __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
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
