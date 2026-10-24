// Check that capture rotation moves an invariant-input filler past its consumer and adds a preheader prologue copy.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -fno-unroll-loops -mtt-tensix-optimize-capture-rotation -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-times "Capture rotation moved uid=\\d+ into the stall after uid=\\d+ with prologue uid=\\d+ target=bh" 2 "rvtt_schedule" } }
// { dg-final { scan-assembler-times "SFPAND" 2 } }
// { dg-final { scan-assembler-times "SFPOR" 2 } }
// { dg-final { scan-assembler-not "SFPNOP" } }

void prologue_fill_rows ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto u = __builtin_rvtt_sfpreadlreg (1);
  auto k1 = __builtin_rvtt_sfpreadlreg (2);
  auto k2 = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto t = __builtin_rvtt_sfpand (k1, k2);
      u = __builtin_rvtt_sfpxor (u, t);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (p, p, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpwritelreg (u, 1);
}

void renamed_bias_rows ()
{
  auto y = __builtin_rvtt_sfpreadlreg (4);
  auto v = __builtin_rvtt_sfpreadlreg (5);
  auto m1 = __builtin_rvtt_sfpreadlreg (6);
  auto m2 = __builtin_rvtt_sfpreadlreg (7);
  for (unsigned trip = 0; trip != 9; ++trip)
    {
      auto s = __builtin_rvtt_sfpor (m1, m2);
      v = __builtin_rvtt_sfpxor (v, s);
      auto q = __builtin_rvtt_sfpmuli (nullptr, y, 0x3f79, 0, 0, 0);
      y = __builtin_rvtt_sfpadd (q, q, 0);
    }
  __builtin_rvtt_sfpwritelreg (y, 4);
  __builtin_rvtt_sfpwritelreg (v, 5);
}
