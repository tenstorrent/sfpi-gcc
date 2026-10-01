// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-dce -fdump-tree-rvtt_dce-details" }
// SFPU dead-code elimination (-mtt-tensix-optimize-dce, default on):
// an intrinsic chain whose result reaches no effectful call is deleted
// -- the generic DCE cannot do it because the intrinsics carry virtual
// operands.
// { dg-final { scan-tree-dump "Deleting unreachable" "rvtt_dce" } }
// { dg-final { scan-assembler-not "SFPADD" } }
// { dg-final { scan-assembler-not "SFPMUL" } }

void dead_chain ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto m = __builtin_rvtt_sfpmul (a, b, 0);
  auto s = __builtin_rvtt_sfpadd (m, b, 0);
  (void) s;
}
