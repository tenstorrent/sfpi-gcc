// Seam ownership on WH: without result scoreboarding the loop-carried
// mad-family seam is a REQUIRED-nop site -- but unlike the in-row sites
// owned by the nop inserter and fill_nop_shadows, no in-row mechanism
// can reach across the backedge.  Capture rotation owns the seam: the
// independent member moves to the tail, the commit guard proves the
// producer's delay probe is quiet afterwards, and the SFPNOP never
// appears (one word AND one cycle per row).  Compare
// capture-rotation-noff-wh.C for the padded shape without the flag.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -fno-unroll-loops -mtt-tensix-optimize-capture-rotation -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-times "Capture rotation moved uid=\\d+ to the seam after uid=\\d+ target=wh" 1 "rvtt_schedule" } }
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
