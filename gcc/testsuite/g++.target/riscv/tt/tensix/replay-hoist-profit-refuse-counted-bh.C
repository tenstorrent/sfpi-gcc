// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -3093 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Counted-loop replay payload" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }

// Three-trip loop around a 20-word serial mad-family payload (per-word
// unique immediates: no repeated factor the sequence former could
// re-segment into a shorter firing sub-candidate): the profitability model gives
// 3*(3900 - 3970) - 2883 = -3093 centislots (execution-bound reissue), below the cost-table minimum
// benefit of 60.  This is the silicon-regressing low-trip long-capture
// shape class and must refuse the hoist.
void counted_refuse_3trip_long ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 3; ++ix)
    {
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3a00, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3a11, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3a22, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3a33, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3a44, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3a55, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3a66, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3a77, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3a88, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3a99, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3aaa, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3abb, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3acc, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3add, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3aee, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3aff, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3b10, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3b21, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3b32, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3b43, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
