// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-replay-exec-record" }
// Execute-while-recording: the hoisted counted-loop capture executes its
// payload at the record, the unrolled launch run is emitted directly at
// the record site, and the first (now redundant) playback disappears:
// one exec-record plus trips-1 launches.
// { dg-final { scan-assembler-times "TTREPLAY\t0, 4, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 4, 0, 0" 3 } }
void exec_record_fire ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
