// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// Reissue-gate discharge (lane FW; this twin previously pinned the
// replay-reissue-latency-unproved refusal on this shape): in
// record-hoist mode the exec-side estimate feeds nothing (pricing is
// pure delivery -- the executed word stream is identical in both
// worlds), and the reissue soundness half is carried structurally by
// the unhoisted world's own playback clones of the same stream, so an
// unaudited-latency producer (SFPLUTFP32, audit deliberately deferred)
// no longer blocks the hoist on proven targets.  QSR keeps the
// refusal (record-hoist-qsr.C); the default hoist model keeps the gate
// (its pricing consumes the estimate).
// { dg-final { scan-rtl-dump "record-hoist: invariant re-record window admitted" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "replay-reissue-latency-unproved" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 7, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 7, 0, 0" 2 } }
void rerecord_unaudited (volatile int *out)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      d = __builtin_rvtt_sfplutfp32_3r (a, b, c, d, 0);
      a = __builtin_rvtt_sfpmul (a, d, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      *out = (int) ix;
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      d = __builtin_rvtt_sfplutfp32_3r (a, b, c, d, 0);
      a = __builtin_rvtt_sfpmul (a, d, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}
