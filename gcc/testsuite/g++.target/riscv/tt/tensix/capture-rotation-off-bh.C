// Default-off: without -mtt-tensix-optimize-capture-rotation the fire
// shapes from capture-rotation-seam-bh.C and -prologue-bh.C are
// untouched and the phase leaves no trace in the dump.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -fno-unroll-loops -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-not "Capture rotation" "rvtt_schedule" } }

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
