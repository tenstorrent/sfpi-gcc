// TU value-reuse near misses: (a) TWO TU writes program the same
// destination with DIFFERENT values -- no unique value exists and the
// candidate refuses even though one write matches; (b) a destination
// programmed through an UNDERIVABLE staging chain (a runtime scalar)
// has no provable value at all.  Both keep prgm-exhausted
// byte-identically; nothing is parked.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "reusing TU-programmed" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "refused .prgm-exhausted" 2 "rvtt_prgm_const" } }

void owner_init_conflict (void)
{
  auto a = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (a, 12);
  auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (b, 12);	/* same dest, other value */
  auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3dd8adac, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (c, 13);
}

void owner_init_runtime (unsigned bits)
{
  auto s = __builtin_rvtt_sfploadi (nullptr, bits, 0, 0, 0);
  __builtin_rvtt_sfpwriteconfig_v (s, 14);	/* underivable value */
}

void kernel_conflicted (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void kernel_runtime_owner (void)
{
  auto y = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      y = __builtin_rvtt_sfpmul (y, bias, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (y, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (y, 1);
}
