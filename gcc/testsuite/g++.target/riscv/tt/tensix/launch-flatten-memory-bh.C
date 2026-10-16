// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// Check that a non-volatile memory store in a delivery loop refuses launch flattening (launch-flatten-memory).
// { dg-final { scan-tree-dump "refused .launch-flatten-memory." "rvtt_launch_flatten" } }

int lf_side_channel[8];

void lf_memory ()
{
  for (int d = 0; d < 8; ++d)
    {
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      // Distinct stores stay in the loop; a single scalar store would be
      // sunk to the exit before this pass runs.
      lf_side_channel[d] = d;
    }
}
