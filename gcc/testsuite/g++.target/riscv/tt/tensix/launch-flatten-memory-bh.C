// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// A memory store in the body: scalar state this census cannot prove
// per-trip, refuse by name.
// { dg-final { scan-tree-dump "refused .launch-flatten-memory." "rvtt_launch_flatten" } }

volatile int lf_side_channel;

void lf_memory ()
{
  for (int d = 0; d < 8; ++d)
    {
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      lf_side_channel = d;
    }
}
