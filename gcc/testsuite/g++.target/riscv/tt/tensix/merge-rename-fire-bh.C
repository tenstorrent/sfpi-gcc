// MERGE-RENAME fire: an in-loop
// constant-immediate CC-merge (single-issue FLOATB sfploadi_lv whose
// live-value link is loop-varying) is renamed under the adjudication
// override into a parked full-lane twin (the pressure-park LREG tier's
// proven programming point) plus an in-loop register-source predicated
// move.  The rolled loop's delivered word count is unchanged (the
// class is word-neutral by construction); only the immediate's value
// naming moves from the merge word to the parked twin.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-residency-merge-rename -mtt-tensix-merge-rename-allow-neutral -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "merge-rename: renamed in-loop immediate CC-merge 0x7fc00000" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "pressure-park: hoisted invariant materialization to a free LREG" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "merge-rename: refused" "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPMOV" } }
// { dg-final { scan-assembler-times "SFPLOADI\tL\\d+, 32704, 0" 2 } }

void merge_rename_fire (void)
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
