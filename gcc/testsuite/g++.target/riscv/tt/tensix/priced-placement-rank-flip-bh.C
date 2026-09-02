// PLACEMENT-ARBITER priced residency ranking fire (the placement arbiter; shadow
// leg = priced-placement-rank-shadow-bh.C, same body): GV's
// uses-then-value key ranks the 3-use cold-loop constant above the
// 1-use hot-loop constant; the arbiter's run-amortized delivery
// benefit (one in-loop word saved per body execution against the
// two-word programming at entry, through the one delivery-cost API and
// the trips facade) ranks the 32-trip constant first.  Under the flag
// the priced order decides who claims PRGM L12 first.  Known-residual
// discharge: the uses-then-value suboptimality for mixed word-weight
// sets was GV's self-named residual (gimple-rvtt-prgm-const.cc).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-priced-placement -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "residency-rank loop-class legacy=.0x3e5b1a3d,0x40a90fdb. priced=.0x40a90fdb,0x3e5b1a3d. DISAGREE .deciding=priced." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "allocated PRGM L12 for constant 0x40a90fdb .loop class, 1 uses" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "allocated PRGM L13 for constant 0x3e5b1a3d .loop class, 3 uses" "rvtt_prgm_const" } }

#define ST(x, r) __builtin_rvtt_sfpstore (nullptr, (x), (r), 0, 0, 4, 7)

void rank_priced_flip (void)
{
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto hot = __builtin_rvtt_sfpxloadi (nullptr, 0x40a90fdb, 0, 0, 31);
      ST (hot, 208);
    }
  for (unsigned jx = 0; jx != 3; ++jx)
    {
      auto cold = __builtin_rvtt_sfpxloadi (nullptr, 0x3e5b1a3d, 0, 0, 31);
      ST (cold, 216); ST (cold, 224); ST (cold, 232);
    }
}
