// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Hoist profitable: modeled benefit 9 >= 0" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Counted-loop replay payload" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }

// Override semantics: a four-word delivery-bound body over 42 trips
// models 42*(492-470) - 915 = 9, below the cost-table minimum of 60 but
// admitted by an explicit -mtt-tensix-replay-hoist-min-benefit=0.
void override_low_margin ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 42; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}
