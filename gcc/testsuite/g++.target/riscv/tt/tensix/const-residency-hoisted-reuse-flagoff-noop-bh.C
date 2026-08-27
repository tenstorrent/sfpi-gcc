// Flag-off identity for the HOISTED-REUSE class: the same TU under
// plain -mtt-tensix-optimize-const-residency never collects an
// out-of-loop materialization (only in-loop candidates exist in the
// established classes), so the hoisted constants keep their LREG
// placements and only the init's own three SFPCONFIGs are emitted.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "hoisted-reuse" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 3 } }

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
