// PRESSURE-PARK LREG tier on a CC-canonical loop programmed on its own
// entry edge.  Four admitted post-CC candidates against three free PRGM
// destinations -- the fourth hits prgm-exhausted and parks in a free
// LREG.  The CC region ahead of the loop ends in the all-lanes SFPENCC,
// so nothing is peeled and there is no peeled copy of the
// materialization to pay for.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "pressure-park: admitted post-CC candidate" 4 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 3 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "const-residency: refused .prgm-exhausted." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "programmed on its all-lanes entry edge" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "pressure-park: hoisted invariant materialization to a free LREG at the programming point" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "peeled first iteration" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 3 } }

void park_prepeel_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc (x, 0);
  __builtin_rvtt_sfppopc (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (x, 0);
      __builtin_rvtt_sfppopc (0);
      auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e11a3b7, 0, 0, 31);
      auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f337ab1, 0, 0, 31);
      auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x40355cf2, 0, 0, 31);
      auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x4111fa9c, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c0, 0);
      x = __builtin_rvtt_sfpmul (x, c1, 0);
      x = __builtin_rvtt_sfpmul (x, c2, 0);
      x = __builtin_rvtt_sfpmul (x, c3, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
