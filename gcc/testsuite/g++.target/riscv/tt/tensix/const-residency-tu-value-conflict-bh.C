/* Runtime immediates need a non-null instruction buffer for the synthesized word.  */
namespace ckernel { volatile unsigned long *instrn_buffer; }
// Check that conflicting or underivable TU PRGM writes prevent value reuse (prgm-exhausted).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "reusing TU-programmed" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "refused .prgm-exhausted" 2 "rvtt_prgm_const" } }

void owner_init_conflict (void)
{
  auto a = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (a, 0, 12);
  auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (b, 0, 12);	/* same dest, other value */
  auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3dd8adac, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (c, 0, 13);
}

void owner_init_runtime (unsigned bits)
{
  auto s = __builtin_rvtt_sfploadi (ckernel::instrn_buffer, bits, 0, 0, 0);
  __builtin_rvtt_sfpwriteconfig_v (s, 0, 14);	/* underivable value */
}

void kernel_conflicted (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
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
      __builtin_rvtt_sfpsetcc (y, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (y, 1);
}
