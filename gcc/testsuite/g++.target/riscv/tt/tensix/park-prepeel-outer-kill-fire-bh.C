// Check that a canonical loop inside an outer loop is programmed on its entry edge and parks the exhausting constant in an LREG.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "programmed on its all-lanes entry edge" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "pressure-park: hoisted invariant materialization to a free LREG at the programming point" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "peeled first iteration" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "park-prepeel-ambient-unproven" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }

void outer_kill_fire (int faces)
{
  // Establish physical full lanes independently of the entry ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (int face = 0; face != faces; ++face)
    {
      for (unsigned ix = 0; ix != 32; ++ix)
	{
	  __builtin_rvtt_sfppushc (0);
	  __builtin_rvtt_sfpsetcc (x, 0);
	  __builtin_rvtt_sfppopc (0);
	  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e93b7a1, 0, 0, 31);
	  auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f4cca13, 0, 0, 31);
	  auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x40871bfe, 0, 0, 31);
	  auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x41217e5c, 0, 0, 31);
	  x = __builtin_rvtt_sfpmul (x, c0, 0);
	  x = __builtin_rvtt_sfpmul (x, c1, 0);
	  x = __builtin_rvtt_sfpmul (x, c2, 0);
	  x = __builtin_rvtt_sfpmul (x, c3, 0);
	}
      __builtin_rvtt_sfpwritelreg (x, 1);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
