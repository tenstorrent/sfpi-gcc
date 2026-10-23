// Check that without -mtt-tensix-optimize-pressure-park no post-CC candidate is admitted.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "pressure-park:" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "peeled first iteration" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }

void park_off_noop (void)
{
  // Establish physical full lanes independently of the entry ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
      auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f21aa52, 0, 0, 31);
      auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x402df854, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c0, 0);
      x = __builtin_rvtt_sfpmul (x, c1, 0);
      x = __builtin_rvtt_sfpmul (x, c2, 0);
      x = __builtin_rvtt_sfpmul (x, c3, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
