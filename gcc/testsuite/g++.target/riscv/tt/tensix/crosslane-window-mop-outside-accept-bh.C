// MOP acceptance control (lane FR): the same counted playback loop with
// no window forms its TTMOP and compiles silently under the crosslane
// flag -- the refusal keys on the proven-OPEN window, not on MOP
// formation itself.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fwhole-program -fkeep-static-functions -fno-unroll-loops -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mtt-tensix-optimize-mop-form" }
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
