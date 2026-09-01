// MERGE-RENAME word-neutral refusal (lane KP, R1(b)): the class flag
// alone -- without the adjudication override -- refuses the rename by
// name: the single-issue CC-merge is one in-loop delivered word before
// and after the rename while the parked twin adds its materialization
// word, so the priced delivery benefit is identically negative.  The
// merge keeps its established in-loop SFPLOADI byte-identically.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-residency-merge-rename -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "merge-rename: refused .merge-rename-word-neutral" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "merge-rename: renamed" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "pressure-park: hoisted invariant materialization" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPLOADI\tL\\d+, 32704, 0" 1 } }

void merge_rename_refuse_neutral (void)
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
