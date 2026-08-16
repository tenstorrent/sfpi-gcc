// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Hoist profitable: modeled benefit 4295 >= 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Counted-loop replay payload" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 2 } }

// Sixteen-trip loop around a 10-slot payload: the profitability model gives
// 16*(123 + 23*10) - 123*11 = 4295 centislots, far above the cost-table
// minimum benefit of 60.  This high-trip shape class must hoist.
void counted_fire_16trip ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 16; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
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
