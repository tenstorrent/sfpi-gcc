// MERGE-RENAME vocabulary refusal (lane KP, R1(b)): a non-FLOATB
// immediate CC-merge (mod 1, FLOATA) is outside the minimal renaming
// vocabulary -- exactly the constant-image derivation
// constant_chain_value_p blesses -- and refuses by name
// (merge-rename-form-unsupported), keeping the established lowering
// byte-identically.  The registry counter censuses the
// out-of-vocabulary breadth corpus-wide.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-residency-merge-rename -mtt-tensix-merge-rename-allow-neutral -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "merge-rename: refused .merge-rename-form-unsupported: non-FLOATB mod" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "merge-rename: renamed" "rvtt_prgm_const" } }

void merge_rename_form_refuse (void)
{
  auto y = __builtin_rvtt_sfpreadlreg (0);
  auto x = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      y = __builtin_rvtt_sfpmul (y, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      y = __builtin_rvtt_sfploadi_lv (nullptr, y, 0x3c00, 0, 0, 1);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (y, 0);
}
