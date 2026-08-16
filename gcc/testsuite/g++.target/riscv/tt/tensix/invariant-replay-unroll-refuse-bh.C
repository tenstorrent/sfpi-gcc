// Near misses: a runtime trip count cannot be proved constant and a
// constant zero-trip loop never executes its body, so neither may receive
// a hoist or an unroll request.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-not "Requested complete unroll" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }

void runtime_bound (unsigned bound)
{
  auto acc = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned remaining = bound; remaining != 0; --remaining)
    {
      auto k = __builtin_rvtt_sfpxloadi (nullptr, 0x3f100001, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 1);
}

void never_entered ()
{
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned remaining = 0; remaining != 0; --remaining)
    {
      auto k = __builtin_rvtt_sfpxloadi (nullptr, 0x3f200001, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, k, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
