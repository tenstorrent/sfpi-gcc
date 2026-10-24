// Check that a TTREPLAY playback before the chain's first link does not block the integer rebalance.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_reassoc" }
// { dg-final { scan-tree-dump-times "reassoc: integer rebalance depth 3->2 .sfpxor chain of 4 terms" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "reassoc-replay-playback-boundary" "rvtt_reassoc" } }

void
ra_xor_replay_outside (void)
{
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 1);
  auto z0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto z1 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto z2 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto z3 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto u1 = __builtin_rvtt_sfpxor (z0, z1);
  auto u2 = __builtin_rvtt_sfpxor (u1, z2);
  auto u3 = __builtin_rvtt_sfpxor (u2, z3);
  __builtin_rvtt_sfpstore (nullptr, u3, 0, 0, 0, 6, 7);
}
