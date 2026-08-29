// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -fdump-tree-rvtt_prgm_const-details" }
// Pre-peel park-placement RENAMED-EQUIVALENT / VARIED-CONSTANTS
// adversary twin (lane IP audit, GY recipe): different function and
// value names, a different input LREG (3), a different trip count
// (17), and four entirely different loop-invariant constants (none
// shared with any board row's coefficients).  The pre-peel LREG-tier
// placement must key on the structural facts alone (post-CC
// candidates exceeding the free PRGM file inside a canonical
// CC-restore loop whose only entry path is the function entry) and
// still erase the peel duplicate.
// { dg-final { scan-tree-dump-times "pressure-park: admitted post-CC candidate" 4 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 3 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "const-residency: refused .prgm-exhausted." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "pressure-park: hoisted invariant materialization to a free LREG at the pre-peel entry .peel bb \\d+; ambient all-lanes proven; peel duplicate erased." 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "free LREG at the programming point" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 3 } }

void audit_ip_ladder (void)
{
  auto acc = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned step = 0; step != 17; ++step)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (acc, 0);
      __builtin_rvtt_sfppopc (0);
      auto k0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3d8f5c29, 0, 0, 31);
      auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb504f3, 0, 0, 31);
      auto k2 = __builtin_rvtt_sfpxloadi (nullptr, 0x4048f5c3, 0, 0, 31);
      auto k3 = __builtin_rvtt_sfpxloadi (nullptr, 0x42280000, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k0, 0);
      acc = __builtin_rvtt_sfpmul (acc, k1, 0);
      acc = __builtin_rvtt_sfpmul (acc, k2, 0);
      acc = __builtin_rvtt_sfpmul (acc, k3, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 3);
}
