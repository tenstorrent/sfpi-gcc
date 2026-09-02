// PRESSURE-PARK fire, LREG tier: four admitted post-CC
// candidates against three free PRGM destinations -- the fourth hits
// the established prgm-exhausted refusal and then hoists to the SAME
// proven programming point (after the peeled iteration's all-lanes
// SFPENCC) as a plain LREG live range, because the function-wide SSA
// pressure model leaves headroom in the 8-LREG file.  This is the
// rename-to-free-LREG admission the early invariant pass refuses under
// its conservative in-loop walk.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "pressure-park: admitted post-CC candidate" 4 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 3 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "const-residency: refused .prgm-exhausted." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "pressure-park: hoisted invariant materialization to a free LREG" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 3 } }

void park_lreg_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
      auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f21aa52, 0, 0, 31);
      auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x402df854, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c0, 0);
      x = __builtin_rvtt_sfpmul (x, c1, 0);
      x = __builtin_rvtt_sfpmul (x, c2, 0);
      x = __builtin_rvtt_sfpmul (x, c3, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
