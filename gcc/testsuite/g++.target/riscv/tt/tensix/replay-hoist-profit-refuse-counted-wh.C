// { dg-options "-mcpu=tt-wh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit " "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-assembler "\\tbne\\t" } }

// Low-trip refusal on WH: a chain of DISTINCT immediate multiplies (no
// repeated subsequence anywhere, so the in-loop replay former has no
// alternative capture) over four trips prices below the cost-table
// minimum and refuses.
void refuse_serial_chain_wh ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3c11, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3c21, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3c31, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3c41, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3c51, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3c61, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3c71, 0, 0, 0);
      x = __builtin_rvtt_sfpmuli (nullptr, x, 0x3c81, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
