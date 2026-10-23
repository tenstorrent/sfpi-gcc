// Check that a CC block reaching the programming point through an outer backedge refuses residency.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "refused .cc-region-unproven.: flags/ROW_MASK unproved" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "const-residency: allocated" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }

void residency_cc_backedge_refuse (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned r = 0; r != 4; ++r)
    {
      for (unsigned ix = 0; ix != 16; ++ix)
	{
	  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
	  x = __builtin_rvtt_sfpmul (x, gain, 0);
	}
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
      /* The balanced region above restores all flags.  Leave a genuinely
	 non-full state on the outer backedge instead of relying on a
	 conservative reachability refusal after a proven restoration.  */
      __builtin_rvtt_sfpencc (10, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
