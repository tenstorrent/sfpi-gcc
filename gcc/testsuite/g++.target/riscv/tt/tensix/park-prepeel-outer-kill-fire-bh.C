// Pre-peel ambient proof, ENCC-kill arm: the softplus
// PRODUCTION anatomy -- the canonical loop sits inside an OUTER loop,
// so its own lowered CC writers reach the pre-peel point around the
// outer backedge.  The plain reachability oracle
// (cc_write_reaches_point_p) would refuse; the kill-aware walk proves
// every such path passes the body's trailing word-exact all-lanes
// SFPENCC (the canonical tail itself), so the ambient is all-lanes and
// the park LREG tier still takes the pre-peel placement and erases the
// peel duplicate.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "pressure-park: hoisted invariant materialization to a free LREG at the pre-peel entry .peel bb \\d+; ambient all-lanes proven; peel duplicate erased." 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "free LREG at the programming point" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "park-prepeel-ambient-unproven" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 3 } }

void outer_kill_fire (int faces)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (int face = 0; face != faces; ++face)
    {
      for (unsigned ix = 0; ix != 32; ++ix)
	{
	  __builtin_rvtt_sfppushc (0);
	  __builtin_rvtt_sfpsetcc_v (x, 0);
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
