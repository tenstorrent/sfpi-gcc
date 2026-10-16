// Check that a counted playback loop with no open window forms its TTMOP silently.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mtt-tensix-optimize-mop-form" }
// { dg-final { scan-assembler-times "TTMOP\t0, 19, 0" 1 } }

void mop_outside_window_control ()
{
  __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 1, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  x = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpwritelreg (x, 0);
  for (unsigned i = 0; i != 20; ++i)
    __builtin_rvtt_ttreplay (nullptr, 3, 0, 0, 0, 0, 0);
}
