// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_crossloop" }
// QSR refuses by pass gate: no validated capability.
// { dg-final { scan-tree-dump "refused .qsr-unproven." "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "hoisted" "rvtt_crossloop" } }

void
xlh_qsr_kernel (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 3);
      __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 3);
    }
}
