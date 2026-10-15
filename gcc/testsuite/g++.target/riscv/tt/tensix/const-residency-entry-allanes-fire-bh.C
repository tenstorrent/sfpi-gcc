// Residency, CC-canonical body class, programmed on the loop's own entry
// edge.  No CC write reaches the preheader of entry_fire's loop, so the
// fn-entry all-lanes state holds there and the body's trailing all-lanes
// SFPENCC carries it into every later trip: the constant is programmed
// once before the loop and nothing is peeled.  Its two same-value
// materializations share one programming.  prior_cc_keeps_peel runs a v_if
// region before its loop; that CC write reaches the preheader, so the
// first-iteration peel stays.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "programmed on its all-lanes entry edge" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "reused PRGM L1\\d for identical constant 0x40000000" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "peeled first iteration" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

void entry_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto zero = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto two = __builtin_rvtt_sfpxloadi (nullptr, 0x40000000, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, two, 0);
      auto again = __builtin_rvtt_sfpxloadi (nullptr, 0x40000000, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, again, 0);
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

void prior_cc_keeps_peel (void)
{
  auto west = __builtin_rvtt_sfpreadlreg (2);
  auto floor = __builtin_rvtt_sfpreadlreg (3);
  __builtin_rvtt_sfppushc (0);
  auto pre = __builtin_rvtt_sfpxpred (0x15, 0);
  auto low = __builtin_rvtt_sfpxcmp (west, floor, 0x12);
  auto done = __builtin_rvtt_sfpxcond (0, pre, low);
  west = __builtin_rvtt_sfpassign_lv (west, floor);
  __builtin_rvtt_sfpxpred (0x10, done);
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
