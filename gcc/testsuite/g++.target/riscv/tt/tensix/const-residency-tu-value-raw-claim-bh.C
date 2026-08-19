// TU value-reuse near miss: a raw `.ttinsn' SFPCONFIG word claims the
// same destination the typed init programs.  A raw word's staged value
// is unauditable (it copies LReg[0]), so the destination loses value
// uniqueness EVEN THOUGH the typed write's value matches the
// candidate: reuse refuses (prgm-exhausted) in either scan order.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "reusing TU-programmed" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "refused .prgm-exhausted" "rvtt_prgm_const" } }

void owner_init_typed (void)
{
  auto ln2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (ln2, 12);
  auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3dd8adac, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k1, 13);
  auto k2 = __builtin_rvtt_sfpxloadi (nullptr, 0xbf317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k2, 14);
}

void owner_init_raw (void)
{
  /* SFPCONFIG dest 12 from LReg[0]: opcode 0x91, dest field 12 --
     claimed with an unauditable value.  */
  asm volatile (".ttinsn %0" :: "i" (0x910000C0));
}

void kernel_no_reuse (void)
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
