// LOOP-RECLAIM reader near-miss twin (lane ID): every claimed slot IS
// read somewhere in the TU (typed sfpreadlreg census), so no claim is
// DEAD -- the reclaim tier finds nothing and every non-matching value
// keeps the established prgm-exhausted refusal, flag on.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-loop-prgm-reclaim -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-not "reclaiming DEAD-claimed" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "loop-reclaim-call-window" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "refused .prgm-exhausted" 2 "rvtt_prgm_const" } }

void owner_init (void)
{
  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (c0, 12);
  auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3dd8adac, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k1, 13);
  auto k2 = __builtin_rvtt_sfpxloadi (nullptr, 0xbf317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k2, 14);
}

void claim_consumer (void)
{
  auto a = __builtin_rvtt_sfpreadlreg (12);
  auto b = __builtin_rvtt_sfpreadlreg (13);
  auto c = __builtin_rvtt_sfpreadlreg (14);
  auto s = __builtin_rvtt_sfpadd (a, b, 0);
  s = __builtin_rvtt_sfpadd (s, c, 0);
  __builtin_rvtt_sfpwritelreg (s, 1);
}

void kernel_inloop (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto h1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
      auto pi = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, h1, 0);
      x = __builtin_rvtt_sfpadd (x, pi, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
