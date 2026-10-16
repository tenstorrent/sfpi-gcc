// Check that a loop programmed on its own all-lanes entry edge reclaims a dead PRGM claim.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-loop-prgm-reclaim -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "programmed on its all-lanes entry edge" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "reclaiming DEAD-claimed PRGM L12 for 0x40000000 .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "loop-reclaim-call-window" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "peeled first iteration" "rvtt_prgm_const" } }

void owner_init (void)
{
  auto k0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3dd8adac, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k0, 0, 12);
  auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e9a209b, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k1, 0, 13);
  auto k2 = __builtin_rvtt_sfpxloadi (nullptr, 0xbf317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k2, 0, 14);
}

void own_entry_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto zero = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto two = __builtin_rvtt_sfpxloadi (nullptr, 0x40000000, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, two, 0);
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
