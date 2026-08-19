// Peel-class near misses on the trip proof: the peel re-delivers one
// body as pushed words and the programming costs pushed words, so the
// break-even trip count must be PROVEN by bounded evaluation of the
// loop's own scalar control (rvtt-cost.md residency-peel model).  A
// runtime trip count does not fold; a two-trip loop folds but sits
// below break-even.  Both refuse by name.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "refused .peel-trip-count-unproven" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "peeled first iteration" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void peel_trip_unproven (unsigned n)
{
  auto x = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != n; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 1);
}

void peel_two_trips (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned ix = 0; ix != 2; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 2);
}
