// PLACEMENT-ARBITER unpriceable near-miss (the placement arbiter): the run-bound of
// the candidates' loop is a runtime parameter the trips facade cannot
// prove, so the priced alternative refuses by name
// (place-alternative-unpriceable) and the whole class keeps GV's
// legacy uses-then-value order byte-identically -- the fail-closed
// direction.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-priced-placement -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "residency-rank loop-class unpriceable .place-alternative-unpriceable: trip weight unproven.; the legacy order stands" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "allocated PRGM L12 for constant 0x3f31aa52 .loop class, 2 uses" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "allocated PRGM L13 for constant 0x40c90fdb .loop class, 1 uses" "rvtt_prgm_const" } }

#define ST(x, r) __builtin_rvtt_sfpstore (nullptr, (x), (r), 0, 0, 4, 7)

void rank_unpriceable (unsigned n)
{
  for (unsigned ix = 0; ix != n; ++ix)
    {
      auto a = __builtin_rvtt_sfpxloadi (nullptr, 0x40c90fdb, 0, 0, 31);
      ST (a, 240);
      auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x3f31aa52, 0, 0, 31);
      ST (b, 248); ST (b, 256);
    }
}
