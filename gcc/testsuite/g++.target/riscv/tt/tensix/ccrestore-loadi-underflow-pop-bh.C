// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// Cross-iteration pop-then-push: the body's first POPC pops a save
// pushed OUTSIDE the loop -- below the loop's own scope -- so no
// in-loop restore fact exists (the state at the load's position is
// owned by stack traffic the loop-local proof cannot see).  Refuse by
// name.
// { dg-final { scan-tree-dump "cc-restore-unbalanced" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }

void crossiter_pop_push ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  __builtin_rvtt_sfppushc (0);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      __builtin_rvtt_sfppopc (0);
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c, 0);
      __builtin_rvtt_sfppushc (0);
    }
  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpwritelreg (x, 0);
}
