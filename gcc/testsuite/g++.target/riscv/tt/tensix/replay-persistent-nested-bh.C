// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// The fixed eight-row SFPU body is recorded before the enclosing eight-page
// loop, then replayed eight times per page.  In particular, the page
// backedge targets the label after the sole record-only capture.
// { dg-final { scan-rtl-dump-times "Persistent counted hoist: loop \\d+ across outer loop \\d+ \\(8 trips\\)" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture \\[0,\\+18\\) to preheader" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 18, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 18, 0, 0" 8 } }

extern "C" void
persistent_nested (volatile int *separator)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned page = 0; page != 8; ++page)
    {
      for (unsigned row = 0; row != 8; ++row)
	{
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  b = __builtin_rvtt_sfpmul (b, c, 0);
	  c = __builtin_rvtt_sfpmul (c, a, 0);
	  a = __builtin_rvtt_sfpmul (a, a, 0);
	  b = __builtin_rvtt_sfpmul (b, b, 0);
	  c = __builtin_rvtt_sfpmul (c, c, 0);
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  b = __builtin_rvtt_sfpmul (b, c, 0);
	  c = __builtin_rvtt_sfpmul (c, a, 0);
	  a = __builtin_rvtt_sfpmul (a, a, 0);
	  b = __builtin_rvtt_sfpmul (b, b, 0);
	  c = __builtin_rvtt_sfpmul (c, c, 0);
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  b = __builtin_rvtt_sfpmul (b, c, 0);
	  c = __builtin_rvtt_sfpmul (c, a, 0);
	  a = __builtin_rvtt_sfpmul (a, a, 0);
	  b = __builtin_rvtt_sfpmul (b, b, 0);
	  c = __builtin_rvtt_sfpmul (c, c, 0);
	}
      *separator = page;
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
