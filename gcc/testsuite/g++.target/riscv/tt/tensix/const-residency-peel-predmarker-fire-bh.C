// Residency fire, CC-canonical peel class, with the explicit-predicate
// ABI's frame and condition tokens in the body (the SFPI v_if spelling).
// The tokens emit no Tensix word once pass_rvtt_vif has lowered the
// condition, so they must neither refuse the CC-canonical proof nor
// move the candidate limit off the first real CC writer.  The twin
// varies names, the constant, the compare and the trip count.
// A CC region ahead of each loop reaches its preheader, so the peel is
// still the only proven all-lanes programming point.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "cc-canonical proof failed" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "admits the CC-canonical peel" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "peeled first iteration" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

void marker_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto zero = __builtin_rvtt_sfpreadlreg (1);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc (x, 0);
  __builtin_rvtt_sfppopc (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfppushc (0);
      auto frame = __builtin_rvtt_sfpxpred (0x15, 0);
      auto cond = __builtin_rvtt_sfpxcmp (x, zero, 0x32);
      auto token = __builtin_rvtt_sfpxcond (0, frame, cond);
      x = __builtin_rvtt_sfpassign_lv (x, zero);
      __builtin_rvtt_sfpxpred (0x10, token);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_varied_clip (void)
{
  auto west = __builtin_rvtt_sfpreadlreg (2);
  auto floor = __builtin_rvtt_sfpreadlreg (3);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc (west, 0);
  __builtin_rvtt_sfppopc (0);
  for (unsigned step = 0; step != 12; ++step)
    {
      auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb, 0, 0, 31);
      west = __builtin_rvtt_sfpmul (west, bias, 0);
      __builtin_rvtt_sfppushc (0);
      auto open = __builtin_rvtt_sfpxpred (0x15, 0);
      auto below = __builtin_rvtt_sfpxcmp (west, floor, 0x12);
      auto shut = __builtin_rvtt_sfpxcond (0, open, below);
      west = __builtin_rvtt_sfpassign_lv (west, floor);
      __builtin_rvtt_sfpxpred (0x10, shut);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (west, 2);
}
