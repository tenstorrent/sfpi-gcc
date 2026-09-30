// STORE-SOURCE TIER mixed-consumer fire: one store consumer
// is enough -- the constant also feeds an SFPMUL, but the SFPSTORE use
// pins the encoding ceiling, and the hoisted plain LREG serves BOTH
// consumers (math reads the LREG exactly as it would read the parked
// register; the store now has a legal L0-L11 source).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-store-source-tier -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "store-source-tier .store-source-encoding-ceiling." 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "pressure-park: hoisted invariant materialization to a free LREG" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-not "SFPMOV" } }

void tier_mixeduse_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfpstore (nullptr, gain, 0, 0, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
