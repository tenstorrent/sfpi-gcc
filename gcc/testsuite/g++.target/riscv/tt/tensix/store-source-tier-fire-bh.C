// STORE-SOURCE TIER fire, plain LOOP class (HL-F1
// generalization): the in-loop invariant constant is consumed as an
// SFPSTORE data source, and SFPSTORE sources L0-L11 only -- a PRGM
// park (L12-L14) would make the register allocator materialize a
// per-row SFPMOV copy out of the constant file.  Under
// -mtt-tensix-optimize-store-source-tier the candidate takes the
// pressure-park LREG tier INSTEAD: the materialization hoists whole to
// the programming point as a plain (SFPSTORE-sourceable) LREG live
// range, no SFPCONFIG is spent, and no copy word exists.  The twin
// function varies names and the constant.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-store-source-tier -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "store-source-tier .store-source-encoding-ceiling." 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "pressure-park: hoisted invariant materialization to a free LREG" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "allocated PRGM" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-not "SFPMOV" } }

void tier_loop_fire (void)
{
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto fill = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, fill, 0, 0, 0, 0, 0);
    }
}

void renamed_varied_splat (void)
{
  for (unsigned step = 0; step != 12; ++step)
    {
      auto seed = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, seed, 0, 0, 0, 0, 2);
    }
}
