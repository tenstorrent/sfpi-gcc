// PLACEMENT-ARBITER ranking shadow leg (item #13; priced leg =
// priced-placement-rank-flip-bh.C, same body): without the flag the
// arbiter dumps the priced order beside GV's uses-then-value order and
// changes nothing -- the 3-use constant keeps PRGM L12
// byte-identically.  The DISAGREE line is the stage-A census key.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "residency-rank loop-class legacy=.0x3e5b1a3d,0x40a90fdb. priced=.0x40a90fdb,0x3e5b1a3d. DISAGREE .deciding=legacy." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "allocated PRGM L12 for constant 0x3e5b1a3d .loop class, 3 uses" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "allocated PRGM L13 for constant 0x40a90fdb .loop class, 1 uses" "rvtt_prgm_const" } }

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
