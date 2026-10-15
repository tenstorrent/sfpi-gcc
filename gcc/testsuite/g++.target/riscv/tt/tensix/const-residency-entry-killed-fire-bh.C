// Residency, CC-canonical body class, nested in an outer loop.  The inner
// body's own CC writers reach its preheader around the outer backedge, but
// every such path leaves through the body's trailing all-lanes SFPENCC, so
// the entry is still all-lanes.  The constant is programmed on the inner
// loop's entry edge and the first iteration is not peeled.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "programmed on its all-lanes entry edge \\(every CC write reaching the preheader is killed" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "peeled first iteration" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }

void nested_fire (unsigned faces)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto zero = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned face = 0; face != faces; ++face)
    {
      for (unsigned ix = 0; ix != 8; ++ix)
	{
	  auto two = __builtin_rvtt_sfpxloadi (nullptr, 0x40000000, 0, 0, 31);
	  x = __builtin_rvtt_sfpmul (x, two, 0);
	  __builtin_rvtt_sfppushc (0);
	  auto frame = __builtin_rvtt_sfpxpred (0x15, 0);
	  auto cond = __builtin_rvtt_sfpxcmp (x, zero, 0x32);
	  auto token = __builtin_rvtt_sfpxcond (0, frame, cond);
	  x = __builtin_rvtt_sfpassign_lv (x, zero);
	  __builtin_rvtt_sfpxpred (0x10, token);
	  __builtin_rvtt_sfppopc (0);
	}
      __builtin_rvtt_sfpwritelreg (x, 2);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
