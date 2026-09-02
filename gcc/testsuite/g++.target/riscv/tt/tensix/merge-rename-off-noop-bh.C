// MERGE-RENAME off-identity (R1(b)): without the class flag
// the walk never collects the merge -- the adjudication override alone
// does nothing (CLASS-B byte-inertness; the flag-off leg is the
// established lowering verbatim).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-merge-rename-allow-neutral -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "merge-rename" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPLOADI\tL\\d+, 32704, 0" 1 } }

void merge_rename_off_noop (void)
{
  auto y = __builtin_rvtt_sfpreadlreg (0);
  auto x = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      y = __builtin_rvtt_sfpmul (y, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      y = __builtin_rvtt_sfploadi_lv (nullptr, y, 0x7fc0, 0, 0, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (y, 0);
}
