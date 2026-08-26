// PRESSURE-PARK near miss, LREG tier budget (lane GV): seven
// live-through vector values plus the in-loop materialization temp put
// the function-wide SSA pressure model at the full 8-LREG file, so
// after the three PRGM destinations are taken the fourth admitted
// candidate refuses by name instead of hoisting -- another loop-wide
// live range would exceed the file.  The refusal changes nothing: the
// materialization stays in the loop byte-identically.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 3 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "const-residency: refused .prgm-exhausted." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "pressure-park: refused .lreg-file-exhausted." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "hoisted invariant materialization to a free LREG" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 3 } }

void park_lreg_refuse (void)
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
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (a0, 0);
      __builtin_rvtt_sfppopc (0);
      auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      a0 = __builtin_rvtt_sfpmul (a0, c0, 0);
      auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f21aa52, 0, 0, 31);
      a0 = __builtin_rvtt_sfpmul (a0, c1, 0);
      auto c2 = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      a0 = __builtin_rvtt_sfpmul (a0, c2, 0);
      auto c3 = __builtin_rvtt_sfpxloadi (nullptr, 0x402df854, 0, 0, 31);
      a0 = __builtin_rvtt_sfpmul (a0, c3, 0);
    }
  __builtin_rvtt_sfpwritelreg (a0, 0);
  __builtin_rvtt_sfpwritelreg (a1, 1);
  __builtin_rvtt_sfpwritelreg (a2, 2);
  __builtin_rvtt_sfpwritelreg (a3, 3);
  __builtin_rvtt_sfpwritelreg (a4, 4);
  __builtin_rvtt_sfpwritelreg (a5, 5);
  __builtin_rvtt_sfpwritelreg (a6, 6);
}
