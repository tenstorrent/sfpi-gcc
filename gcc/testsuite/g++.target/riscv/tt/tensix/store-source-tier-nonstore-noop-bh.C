// STORE-SOURCE TIER non-store no-op: the knob touches ONLY
// store-consumed candidates.  A loop constant consumed by SFPMUL alone
// has no encoding ceiling -- constant registers are legal SFPMUL
// sources -- so the established residency park proceeds untouched and
// the tier machinery contributes nothing.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-store-source-tier -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "store-source-tier" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x3e4b1a3d .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPCONFIG" } }
// (The accumulator's own SFPMOV shuffles are fine -- what must not
// exist is a copy OUT OF the constant file.)
// { dg-final { scan-assembler-not "SFPMOV\tL\[0-9\]+, L1\[2-4\]" } }

void tier_nonstore_noop (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
