// Check that without -mtt-tensix-optimize-hoisted-prgm-reuse hoisted constants keep their LREGs.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "hoisted-reuse" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }

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
