// { dg-options "-mcpu=tt-wh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-replay-exec-record" }
// Wormhole twin of replay-exec-record-bh.C.
// { dg-final { scan-assembler-times "TTREPLAY\t0, 8, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 8, 0, 0" 3 } }
void exec_record_fire_wh ()
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
