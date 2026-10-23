// Check that a post-CC candidate consumed by a cross-lane SFPSHFT2 refuses by name and nothing is hoisted.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "pressure-park: refused .consumer-lane-discipline-unaudited." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "pressure-park: admitted post-CC candidate" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 1 } }

void park_consumer_refuse (void)
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
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      auto shuf = __builtin_rvtt_sfpshft2_subvec_shfl1 (gain, 3);
      x = __builtin_rvtt_sfpmul (x, shuf, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
