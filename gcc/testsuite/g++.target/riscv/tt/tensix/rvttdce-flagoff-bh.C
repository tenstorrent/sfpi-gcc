// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mno-tt-tensix-optimize-dce" }
// Check that with the SFPU DCE disabled the dead intrinsic chain is emitted.
// { dg-final { scan-assembler "SFPMUL" } }
// { dg-final { scan-assembler "SFPADD" } }

void dead_chain ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto m = __builtin_rvtt_sfpmul (a, b, 0);
  // Two uses keep the multiply from contracting into a single MAD.
  auto s = __builtin_rvtt_sfpadd (m, m, 0);
  (void) s;
}
