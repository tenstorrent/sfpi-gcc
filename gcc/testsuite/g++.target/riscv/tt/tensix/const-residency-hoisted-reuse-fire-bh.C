// Check that -mtt-tensix-optimize-hoisted-prgm-reuse reuses a matching PRGM value and reclaims an unread PRGM slot.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-hoisted-prgm-reuse -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "hoisted-reuse loop bb \\d+ candidate: out-of-loop constant" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "reusing TU-programmed PRGM L12 .every TU write stores 0x3f317218" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "reclaiming DEAD-claimed PRGM L13 for 0x40490fdb" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .hoisted-reuse class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "refused .prgm-exhausted" "rvtt_prgm_const" } }
// The init's three SFPCONFIGs plus the reusing kernel's two.
// { dg-final { scan-assembler-times "SFPCONFIG" 7 } }

void owner_init (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto ln2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (ln2, 0, 12);
  auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3dd8adac, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k1, 0, 13);
  auto k2 = __builtin_rvtt_sfpxloadi (nullptr, 0xbf317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k2, 0, 14);
}

void kernel_hoisted (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
  auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      x = __builtin_rvtt_sfpadd (x, bias, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
