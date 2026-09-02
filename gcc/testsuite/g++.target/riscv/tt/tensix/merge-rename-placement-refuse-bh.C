// MERGE-RENAME placement refusal (lane KP, R1(b)): seven live-through
// vector values put the function-wide SSA pressure model at the full
// 8-LREG file, so the LREG tier cannot place the full-lane twin
// (lreg-file-exhausted) and the rename refuses by name WITHOUT
// touching the merge -- the fail-closed leg keeps the in-loop
// SFPLOADI byte-identically even under the adjudication override.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-residency-merge-rename -mtt-tensix-merge-rename-allow-neutral -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "pressure-park: refused .lreg-file-exhausted." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "merge-rename: refused .merge-rename-placement-refused" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "merge-rename: renamed" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPLOADI\tL\\d+, 32704, 0" 1 } }

void merge_rename_placement_refuse (void)
{
  auto a0 = __builtin_rvtt_sfpreadlreg (0);
  auto a1 = __builtin_rvtt_sfpreadlreg (1);
  auto a2 = __builtin_rvtt_sfpreadlreg (2);
  auto a3 = __builtin_rvtt_sfpreadlreg (3);
  auto a4 = __builtin_rvtt_sfpreadlreg (4);
  auto a5 = __builtin_rvtt_sfpreadlreg (5);
  auto a6 = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      a0 = __builtin_rvtt_sfpmul (a0, a1, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (a1, 0);
      a0 = __builtin_rvtt_sfploadi_lv (nullptr, a0, 0x7fc0, 0, 0, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (a0, 0);
  __builtin_rvtt_sfpwritelreg (a1, 1);
  __builtin_rvtt_sfpwritelreg (a2, 2);
  __builtin_rvtt_sfpwritelreg (a3, 3);
  __builtin_rvtt_sfpwritelreg (a4, 4);
  __builtin_rvtt_sfpwritelreg (a5, 5);
  __builtin_rvtt_sfpwritelreg (a6, 6);
}
