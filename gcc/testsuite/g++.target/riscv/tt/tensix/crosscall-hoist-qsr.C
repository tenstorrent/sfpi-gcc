// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// Check that the cross-call hoist is gated off on Quasar.
// { dg-final { scan-tree-dump "refused .qsr-unproven." "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "hoisted" "rvtt_crosscall" } }

__attribute__((noinline)) void
cch_qsr_callee ()
{
  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 3);
  __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 3);
}

void
cch_qsr_caller (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    cch_qsr_callee ();
}
