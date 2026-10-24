// Check that without -mtt-tensix-optimize-loop-prgm-reclaim no dead PRGM claim is reclaimed.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "reusing TU-programmed PRGM L12 .every TU write stores 0x3e317218" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "reclaiming DEAD-claimed" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "loop-reclaim-call-window" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "refused .prgm-exhausted" 4 "rvtt_prgm_const" } }
// The init's three SFPCONFIGs plus one programming shared by the
// value-identical pair.
// { dg-final { scan-assembler-times "SFPCONFIG" 6 } }

void owner_init (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (c0, 0, 12);
  auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3dd8adac, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k1, 0, 13);
  auto k2 = __builtin_rvtt_sfpxloadi (nullptr, 0xbf317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k2, 0, 14);
}

void kernel_inloop (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto g1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e317218, 0, 0, 31);
      auto g2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e317218, 0, 0, 31);
      auto h1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
      auto h2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
      auto pi = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      auto fv = __builtin_rvtt_sfpxloadi (nullptr, 0x40b00000, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, g1, 0);
      x = __builtin_rvtt_sfpadd (x, g2, 0);
      x = __builtin_rvtt_sfpmul (x, h1, 0);
      x = __builtin_rvtt_sfpadd (x, h2, 0);
      x = __builtin_rvtt_sfpmul (x, pi, 0);
      x = __builtin_rvtt_sfpadd (x, fv, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
