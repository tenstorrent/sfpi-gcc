// Check that capture rotation moves an independent row member to the tail to fill a loop-carried stall.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -fno-unroll-loops -mtt-tensix-optimize-capture-rotation -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-times "Capture rotation moved uid=\\d+ to the seam after uid=\\d+ target=bh" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "with prologue" "rvtt_schedule" } }
// { dg-final { scan-assembler-not "SFPNOP" } }

void seam_fill_rows ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      c = __builtin_rvtt_sfpand (c, c);
      d = __builtin_rvtt_sfpor (d, d);
      x = __builtin_rvtt_sfpmul (p, p, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}

void renamed_scaled_rows ()
{
  auto y = __builtin_rvtt_sfpreadlreg (4);
  auto e = __builtin_rvtt_sfpreadlreg (5);
  auto f = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned trip = 0; trip != 12; ++trip)
    {
      auto q = __builtin_rvtt_sfpmuli (nullptr, y, 0x3f81, 0, 0, 0);
      e = __builtin_rvtt_sfpxor (e, e);
      f = __builtin_rvtt_sfpand (f, f);
      y = __builtin_rvtt_sfpadd (q, q, 0);
    }
  __builtin_rvtt_sfpwritelreg (y, 4);
  __builtin_rvtt_sfpwritelreg (e, 5);
  __builtin_rvtt_sfpwritelreg (f, 6);
}
