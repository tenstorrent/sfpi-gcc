// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti --param=max-completely-peeled-insns=1 -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// Read/write annotations alone must not admit the replay-owner-only class.
// Existing raw-only, trip-count and size-budget tests must remain unchanged.
// { dg-final { scan-tree-dump "refused .launch-flatten-no-typed-content." "rvtt_launch_flatten" } }
// { dg-final { scan-tree-dump-not "launch-flatten: requested complete unroll" "rvtt_launch_flatten" } }

void lf_lreg_owners ()
{
  for (unsigned d = 0; d < 8; ++d)
    {
      auto a = __builtin_rvtt_sfpreadlreg (0);
      __builtin_rvtt_sfpwritelreg (a, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
    }
}
