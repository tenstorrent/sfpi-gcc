// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-signed-zeros" }
// Arithmetic by 1, -1 or 0 is not a bitwise identity on Tensix:
// SFPMUL/SFPADD flush denormals and canonicalize NaNs.  The operations
// must survive even when signed zeros are not honored.
// { dg-final { scan-assembler-times "SFPMUL" 2 } }
// { dg-final { scan-assembler-times "SFPADD" 1 } }

void mul_one ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto one = __builtin_rvtt_sfpreadlreg (10);
  auto lv = __builtin_rvtt_sfpreadlreg (2);
  auto r = __builtin_rvtt_sfpmul_lv (lv, a, one, 0);
  __builtin_rvtt_sfpwritelreg (r, 3);
}

void mul_neg_one ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto neg_one = __builtin_rvtt_sfpreadlreg (11);
  auto lv = __builtin_rvtt_sfpreadlreg (2);
  auto r = __builtin_rvtt_sfpmul_lv (lv, a, neg_one, 0);
  __builtin_rvtt_sfpwritelreg (r, 3);
}

void add_zero ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto zero = __builtin_rvtt_sfpreadlreg (9);
  auto lv = __builtin_rvtt_sfpreadlreg (2);
  auto r = __builtin_rvtt_sfpadd_lv (lv, a, zero, 0);
  __builtin_rvtt_sfpwritelreg (r, 3);
}
