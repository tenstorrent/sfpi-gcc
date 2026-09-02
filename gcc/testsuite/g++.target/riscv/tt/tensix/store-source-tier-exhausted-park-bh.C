// STORE-SOURCE TIER near miss, LREG budget: seven
// live-through vector values plus the in-loop materialization temp put
// the function-wide SSA pressure model at the full 8-LREG file, so
// the tier's hoist refuses by the established name
// (lreg-file-exhausted) and the store-consumed
// candidate falls through to the established park -- SFPCONFIG and the
// per-row SFPMOV copy remain, never the bare in-loop
// rematerialization.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-store-source-tier -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "store-source-tier .store-source-encoding-ceiling." 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "pressure-park: refused .lreg-file-exhausted." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x3e4b1a3d .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPCONFIG" } }
// { dg-final { scan-assembler "SFPMOV" } }

void tier_exhausted_park (void)
{
  auto a0 = __builtin_rvtt_sfpreadlreg (0);
  auto a1 = __builtin_rvtt_sfpreadlreg (1);
  auto a2 = __builtin_rvtt_sfpreadlreg (2);
  auto a3 = __builtin_rvtt_sfpreadlreg (3);
  auto a4 = __builtin_rvtt_sfpreadlreg (4);
  auto a5 = __builtin_rvtt_sfpreadlreg (5);
  auto a6 = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto fill = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, fill, 0, 0, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (a0, 0);
  __builtin_rvtt_sfpwritelreg (a1, 1);
  __builtin_rvtt_sfpwritelreg (a2, 2);
  __builtin_rvtt_sfpwritelreg (a3, 3);
  __builtin_rvtt_sfpwritelreg (a4, 4);
  __builtin_rvtt_sfpwritelreg (a5, 5);
  __builtin_rvtt_sfpwritelreg (a6, 6);
}
