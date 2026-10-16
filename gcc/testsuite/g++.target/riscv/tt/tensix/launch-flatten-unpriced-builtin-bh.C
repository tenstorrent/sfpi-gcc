// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// Check that a config builtin outside the priced delivery vocabulary refuses launch flattening (launch-flatten-unpriced-builtin).
// { dg-final { scan-tree-dump "refused .launch-flatten-unpriced-builtin." "rvtt_launch_flatten" } }

void lf_unpriced ()
{
  for (int d = 0; d < 8; ++d)
    {
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_ttreplay (nullptr, 9, 0, 0, 16, 0, 0);
      __builtin_rvtt_sfpconfig_i (0x4, 15, 1);
    }
}
