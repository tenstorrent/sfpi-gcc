// cc-region near miss for the reach-scoped proof (widened admission): a
// CC block AFTER the candidate loop but INSIDE an enclosing loop
// reaches the programming point through the outer backedge -- the
// second outer iteration enters the inner loop with a lane state the
// CC block wrote -- so the candidate still refuses by name and nothing
// changes.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "refused .cc-region-unproven.: a CC write reaches the programming point" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "const-residency: allocated" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void residency_cc_backedge_refuse (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned r = 0; r != 4; ++r)
    {
      for (unsigned ix = 0; ix != 16; ++ix)
	{
	  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
	  x = __builtin_rvtt_sfpmul (x, gain, 0);
	}
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
