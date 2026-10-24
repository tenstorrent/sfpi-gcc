// Unknown playback can itself contain a recorder. Even explicit flag/config
// resets after it do not prove that subsequent words execute rather than being
// captured. Refuse the inner programming and peel, not just outer placement.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-crossloop-cc-peel -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "const-residency: loop bb \\d+ refused .sfpu-barrier." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "cc-peel placement lifted" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "peeled first iteration" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// The two explicit resets only; no speculative PRGM programming.
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

void ccpeel_replay_refuse (int tiles)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  for (int t = 0; t != tiles; ++t)
    {
      __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);
      // An unknown replay may alter both flags and LaneConfig.
      __builtin_rvtt_sfpencc_all_lanes ();
      __builtin_rvtt_sfpconfig_i (0, 15, 1);
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
	  __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}
