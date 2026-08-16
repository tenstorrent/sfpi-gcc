// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Hoist profitable: modeled benefit 121 >= 60" 2 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Counted-loop replay payload" 2 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 2 "rvtt_replay" } }

// Four-trip loop around an 8-slot payload: the silicon-winning short-payload
// low-trip class (21.5 cycles/body on Blackhole, rvtt-cost.md calibration).
// Modeled benefit 4*(123 + 23*8) - 123*9 = 121 centislots >= 60: the hoist
// must fire at the DEFAULT threshold.  The renamed, constant-varied twin
// (different function name, opcode, and LREG) must make the same decision,
// which depends only on the provable trip count, the capture length, and
// the cost table.

void lowtrip_fire_8slot ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void a_completely_unrelated_name (void)
{
  auto w = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned rep = 0; rep != 4; ++rep)
    {
      w = __builtin_rvtt_sfpadd (w, w, 0);
      w = __builtin_rvtt_sfpadd (w, w, 0);
      w = __builtin_rvtt_sfpadd (w, w, 0);
      w = __builtin_rvtt_sfpadd (w, w, 0);
      w = __builtin_rvtt_sfpadd (w, w, 0);
      w = __builtin_rvtt_sfpadd (w, w, 0);
      w = __builtin_rvtt_sfpadd (w, w, 0);
      w = __builtin_rvtt_sfpadd (w, w, 0);
    }
  __builtin_rvtt_sfpwritelreg (w, 3);
}
