// Materialized SFPADD shape with the verbatim-image loadi forms
// (widened admission): the sfpadd arm's value recovery now runs through
// the same audited single-issue-chain derivation as the residency
// classes, so the canonical sfpxloadi mod-31 (int bit-image) constant
// the frontend emits is admitted -- previously only the mod -32
// float-typed form was.  The mul-side coefficient stays materialized
// (not in this admission; the fused-MAD arm covers the already-fused
// form).  RECOGNITION-ONLY invariant: the pass itself never fuses --
// its gimple output keeps the mul and add as separate statements (the
// dump-not below proves no sfpmad appears at 293t); the SFPMADs in the
// final assembly come from the pre-existing downstream mul+add->mad
// combine, which fuses this shape identically when the pass is off
// (fusing in gimple here would collapse two roundings into one --
// bit-changing -- and is banned).  The second function is the renamed,
// constant-varied twin.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "prgm-const: allocated PRGM L1\\d for invariant immediate" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "sfpmad" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }
// { dg-final { scan-assembler-times "SFPMAD" 2 } }

void muladd_fire ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto k = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb8aa3b, 0, 0, 31);
      auto prod = __builtin_rvtt_sfpmul (x, k, 0);
      auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
      x = __builtin_rvtt_sfpadd (prod, b, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_offset_blend ()
{
  auto acc = __builtin_rvtt_sfpreadlreg (4);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto g = __builtin_rvtt_sfpxloadi (nullptr, 0x3f0f5c29, 0, 0, 31);
      auto scaled = __builtin_rvtt_sfpmul (acc, g, 0);
      auto off = __builtin_rvtt_sfpxloadi (nullptr, 0x3d4ccccd, 0, 0, 31);
      acc = __builtin_rvtt_sfpadd (scaled, off, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 4);
}
