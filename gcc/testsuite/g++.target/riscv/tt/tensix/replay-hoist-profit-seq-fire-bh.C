// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Hoist profitable: modeled benefit 8900 >= 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture" "rvtt_replay" } }

// Repeated-sequence hoist at the DEFAULT threshold: forty trips re-record
// the repeated body sequence every trip.  The serially-chained four-mul
// payload interlocks to 7 slots (mad-family result latency 1):
// exec = 700 >= deliver_record 615, an EXECUTION-bound re-record, so
// the model prices the in-loop record pass at exec + RECORD_OVERHEAD
// and the hoisted preheader pass at RECORD_OVERHEAD alone:
// 40 * ((700 + 300) - (700 + 70)) - 300 = 8900 >= 60 and the capture
// moves to the preheader as a record-only pass.
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
