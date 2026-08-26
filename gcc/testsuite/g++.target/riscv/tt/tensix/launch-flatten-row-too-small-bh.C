// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// Fewer delivered words per trip than the row minimum cannot price the
// removed loop-control words against growth: refuse by name.
// { dg-final { scan-tree-dump "refused .launch-flatten-row-too-small." "rvtt_launch_flatten" } }

void lf_small ()
{
  for (int d = 0; d < 8; ++d)
    {
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
    }
}
