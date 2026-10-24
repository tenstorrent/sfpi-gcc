// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Hoist profitable: modeled benefit 8900 >= 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture" "rvtt_replay" } }

// Check that a forty-trip execution-bound repeated sequence hoists its capture at the default threshold.
void seq_fire_40trip ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto y = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 40; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpwritelreg (y, 1);
}
