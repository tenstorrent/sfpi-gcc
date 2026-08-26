// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// An explicit user annotation is never overridden: `#pragma GCC unroll 1'
// keeps even a fully admissible delivery loop rolled.
// { dg-final { scan-tree-dump-not "requested complete unroll" "rvtt_launch_flatten" } }
// { dg-final { scan-assembler-times "TTREPLAY\t16, 9, 0, 0" 4 } }

void lf_user_annotation ()
{
#pragma GCC unroll 1
  for (int d = 0; d < 8; ++d)
    {
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
    }
}
