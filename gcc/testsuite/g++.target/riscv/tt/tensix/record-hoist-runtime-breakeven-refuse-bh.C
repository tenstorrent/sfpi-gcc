// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay-details" }
// Named refusal: a runtime-trip loop whose window is too
// small to clear the 2-trip break-even (per_trip = 4*123 - 70 = 422;
// record_once = 5*123 + 300 = 915; 2-trip benefit = -71 < 60) refuses
// by name and keeps the in-body exec-record byte-identically -- the
// unknown trip count may never repay the preheader record.
// { dg-final { scan-rtl-dump "record-hoist refused: record-hoist-runtime-trips-break-even: 2-trip benefit -71 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
void rerecord_runtime_small_window (unsigned n)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  volatile unsigned *fifo = (volatile unsigned *) 0xFFE40000u;
  for (unsigned ix = 0; ix != n; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, a, 0);
      *fifo = 0xB2010000u + ((ix & 1u) << 9);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, a, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
}
