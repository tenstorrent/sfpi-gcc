// Check that a CC write reaching a hoisted materialization refuses PRGM reuse (cc-region-unproven).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-hoisted-prgm-reuse -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "refused .cc-region-unproven.: flags/ROW_MASK unproved" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "hoisted-reuse class" "rvtt_prgm_const" } }
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

void kernel_cc_before_point (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  /* A CC region ahead of the hoisted materialization: the reach test
     covers the in-place programming point.  */
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc (x, 0);
  x = __builtin_rvtt_sfpadd (x, x, 0);
  __builtin_rvtt_sfppopc (0);
  /* A balanced pop restores the proven entry state; this explicit enable
     change makes the actual materialization point non-full.  */
  __builtin_rvtt_sfpencc (10, 0);
  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
