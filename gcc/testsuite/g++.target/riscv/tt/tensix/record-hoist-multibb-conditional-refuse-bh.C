// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay-details" }
// Named refusal (lane FW): the capture bb executes CONDITIONALLY inside
// the loop (it does not dominate the latch), so the per-trip delivery
// saving the pricing consumes is not structural -- refuse by the shape
// name and keep the in-body record.
// { dg-final { scan-rtl-dump "record-hoist refused: record-hoist-loop-shape: capture bb \\d+ does not dominate the latch" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
static volatile int cb_ticks;
void rerecord_conditional_capture (unsigned n, unsigned sel)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned ix = 0; ix != n; ++ix)
    {
      if (ix != sel)		// capture bb is conditional
	{
	  a = __builtin_rvtt_sfpmul (a, a, 0);
	  b = __builtin_rvtt_sfpmul (b, b, 0);
	  c = __builtin_rvtt_sfpmul (c, c, 0);
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  b = __builtin_rvtt_sfpmul (b, c, 0);
	  c = __builtin_rvtt_sfpmul (c, a, 0);
	  cb_ticks = (int) ix;
	  a = __builtin_rvtt_sfpmul (a, a, 0);
	  b = __builtin_rvtt_sfpmul (b, b, 0);
	  c = __builtin_rvtt_sfpmul (c, c, 0);
	  a = __builtin_rvtt_sfpmul (a, b, 0);
	  b = __builtin_rvtt_sfpmul (b, c, 0);
	  c = __builtin_rvtt_sfpmul (c, a, 0);
	}
      cb_ticks = (int) ix;
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
