// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mno-tt-tensix-optimize-dce" }
// Flag-off twin of rvttdce-dead-chain-fire-bh.C: with the pass
// disabled the dead intrinsic chain is emitted (its declared side
// effects shield it from the generic DCE).
// { dg-final { scan-assembler "SFPMUL" } }
// { dg-final { scan-assembler "SFPADD" } }

void dead_chain ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto m = __builtin_rvtt_sfpmul (a, b, 0);
  auto s = __builtin_rvtt_sfpadd (m, b, 0);
  (void) s;
}
