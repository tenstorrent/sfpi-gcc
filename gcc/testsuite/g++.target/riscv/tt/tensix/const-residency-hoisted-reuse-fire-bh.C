// HOISTED-REUSE class (-mtt-tensix-optimize-hoisted-prgm-reuse):
// loop-invariant constant materializations already parked OUTSIDE the
// loop (the invariant pass's preheader discipline; spelled hoisted
// here) occupy loop-wide LREG live ranges.  Under the flag they
// re-claim PRGM registers through the established placement machinery.
// Both re-claim mechanisms fire here:
//  - `gain' matches the init's L12 value bit-exactly -> TU
//    value-identical reuse (idempotent in-place reprogramming);
//  - `bias' matches no claim, but NO statement in the TU ever reads
//    L13 (the creg_read no-reader proof) and the programming-to-
//    readers window is call-free -> DEAD-claim reclaim with the new
//    value.
// Both materializations become zero-pressure constant-register reads,
// releasing their LREGs.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-hoisted-prgm-reuse -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "hoisted-reuse loop bb \\d+ candidate: out-of-loop constant" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "reusing TU-programmed PRGM L12 .every TU write stores 0x3f317218" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "reclaiming DEAD-claimed PRGM L13 for 0x40490fdb" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .hoisted-reuse class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "refused .prgm-exhausted" "rvtt_prgm_const" } }
// The init's three SFPCONFIGs plus the reusing kernel's two.
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }

void owner_init (void)
{
  auto ln2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (ln2, 12);
  auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3dd8adac, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k1, 13);
  auto k2 = __builtin_rvtt_sfpxloadi (nullptr, 0xbf317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k2, 14);
}

void kernel_hoisted (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
  auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      x = __builtin_rvtt_sfpadd (x, bias, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
