// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// A config-programming builtin outside the priced delivery vocabulary:
// refuse by name (fail closed; the raw-word spelling of the same config
// write is priced, the typed owner is not -- yet).
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
