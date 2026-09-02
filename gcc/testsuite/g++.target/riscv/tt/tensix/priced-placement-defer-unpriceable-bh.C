// PLACEMENT-ARBITER monotone fail-closed near-miss (the placement arbiter): the
// capacity query says the full candidate set does not fit (priced =
// defer) while the legacy demand cut says keep (in-region 1 < 3).
// The defer side's value is unpriceable at the early pass -- it hands
// the candidates to the late walk, whose admission proofs live there
// and may refuse them all (the trigonometry census anatomy; the
// demonstrated-regression lesson) -- so the priced defer refuses by name and the
// legacy keep stands byte-identically even under the flag.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -mtt-tensix-optimize-priced-placement -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump "park-ordering loop bb \\d+ .in-region 1. legacy=keep priced=defer DISAGREE .deciding=legacy: the defer side is unpriceable." "rvtt_invariant" } }
// { dg-final { scan-tree-dump "priced defer refused .place-alternative-unpriceable: the late walk's re-placement is unproven here.; the legacy keep stands" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "defers wholesale" "rvtt_invariant" } }

void park_failclosed_keep (void)
{
  auto x0 = __builtin_rvtt_sfpreadlreg (0);
  auto x1 = __builtin_rvtt_sfpreadlreg (1);
  auto x2 = __builtin_rvtt_sfpreadlreg (2);
  auto x3 = __builtin_rvtt_sfpreadlreg (3);
  auto x4 = __builtin_rvtt_sfpreadlreg (4);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto d0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb8aa3b, 0, 0, 31);
      x0 = __builtin_rvtt_sfpmul (x0, d0, 0);
      auto d1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
      x1 = __builtin_rvtt_sfpmul (x1, d1, 0);
      auto d2 = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      x2 = __builtin_rvtt_sfpmul (x2, d2, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x3, 0);
      auto g0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x4 = __builtin_rvtt_sfpmul (x4, g0, 0);
      __builtin_rvtt_sfppopc (0);
      x3 = __builtin_rvtt_sfpmul (x3, x4, 0);
    }
  __builtin_rvtt_sfpwritelreg (x0, 0);
  __builtin_rvtt_sfpwritelreg (x1, 1);
  __builtin_rvtt_sfpwritelreg (x2, 2);
  __builtin_rvtt_sfpwritelreg (x3, 3);
  __builtin_rvtt_sfpwritelreg (x4, 4);
}
