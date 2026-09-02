// All-lanes proof near miss: a CC write that can execute BEFORE the
// in-place programming points (here it precedes the materializations
// in the same block) defeats the all-lanes proof (SFPCONFIG requires
// every lane enabled: the reference simulator tensix.cpp:9665 asserts it); every
// pressure candidate refuses by name and the pressure error stays
// named.  (The reach-scoped widening admits only CC writes the
// point can never be reached from -- a preceding write is exactly the
// defeating case.)
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "refused .cc-region-unproven.: a CC write reaches the in-place programming point" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "refused .cc-region-unproven. -- in-function CC writes reach every candidate programming point" "rvtt_prgm_const" } }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

void residency_cc_refuse (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  /* A CC write before every candidate materialization.  */
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (x, 0);
  __builtin_rvtt_sfppopc (0);
  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0000, 0, 0, 31);
  auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0001, 0, 0, 31);
  auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0002, 0, 0, 31);
  auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0003, 0, 0, 31);
  auto c4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0004, 0, 0, 31);
  auto c5 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0005, 0, 0, 31);
  auto c6 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0006, 0, 0, 31);
  auto c7 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0007, 0, 0, 31);
  auto c8 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0008, 0, 0, 31);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      x = __builtin_rvtt_sfpmad (x, c0, c1, 0);
      x = __builtin_rvtt_sfpmad (x, c2, c3, 0);
      x = __builtin_rvtt_sfpmad (x, c4, c5, 0);
      x = __builtin_rvtt_sfpmad (x, c6, c7, 0);
      x = __builtin_rvtt_sfpmad (x, c8, c0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
