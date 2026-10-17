// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -696 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }

// Check that a low-trip delivery-bound repeated sequence keeps its capture in the loop.
void seq_refuse_3trip ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto y = __builtin_rvtt_sfpreadlreg (1);
  auto z = __builtin_rvtt_sfpreadlreg (2);
  auto w = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 3; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      z = __builtin_rvtt_sfpmul (z, z, 0);
      w = __builtin_rvtt_sfpmul (w, w, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      w = __builtin_rvtt_sfpmul (w, w, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      z = __builtin_rvtt_sfpmul (z, z, 0);
      w = __builtin_rvtt_sfpmul (w, w, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      z = __builtin_rvtt_sfpadd (z, z, 0);
      w = __builtin_rvtt_sfpmul (w, w, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpwritelreg (y, 1);
  __builtin_rvtt_sfpwritelreg (z, 2);
  __builtin_rvtt_sfpwritelreg (w, 3);
}
