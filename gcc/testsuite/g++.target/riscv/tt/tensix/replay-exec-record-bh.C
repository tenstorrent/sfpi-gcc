// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-replay-exec-record" }
// Execute-while-recording: the hoisted counted-loop capture executes its
// payload at the record, the unrolled launch run is emitted directly at
// the record site, and the first (now redundant) playback disappears:
// one exec-record plus trips-1 launches.  Delivery-bound body (corrected
// reissue pricing: 16*114 - 1407 = 417 >= 0).
// { dg-final { scan-assembler-times "TTREPLAY\t0, 8, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 8, 0, 0" 15 } }
void exec_record_fire ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 16; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
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
