// PARK-SEED COMPOSITION LUT gate: a CC-restore loop whose
// body carries LUT machinery (SFPLUTFP32) keeps the ESTABLISHED
// wholesale deferral even for depth-zero candidates
// (lut-coefficient-authority): the in-loop constant materializations
// are LUT slot coefficients whose placement belongs to the lut-select
// passes — an early hoist moves them out from under that discovery
// (sigmoid-appx-tree anatomy: the 5-word LUT row decays to a
// mov-laden 7-word body, 29861 -> 43447 cycles).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "defers wholesale: lut-coefficient-authority" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "hoist deferred: residency-walk-ordering" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "lut-coefficient constants" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "depth-zero-hoist-dominant" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }

void lut_body_defers_wholesale (void)
{
  auto t0 = __builtin_rvtt_sfpreadlreg (0);
  auto t1 = __builtin_rvtt_sfpreadlreg (1);
  auto t2 = __builtin_rvtt_sfpreadlreg (2);
  auto x = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto coeff = __builtin_rvtt_sfpxloadi (nullptr, 0x3e9c0d51, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, coeff, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
      x = __builtin_rvtt_sfplutfp32_3r (t0, t1, t2, x, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 3);
}
