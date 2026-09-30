// The WH control for capture-rotation-seam-wh.C: without the rotation
// flag the loop-carried mad-family seam pads with a replayed SFPNOP --
// the exact word (and cycle) the seam fill removes.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -fno-unroll-loops -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-not "Capture rotation" "rvtt_schedule" } }
// { dg-final { scan-assembler "SFPNOP" } }

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
