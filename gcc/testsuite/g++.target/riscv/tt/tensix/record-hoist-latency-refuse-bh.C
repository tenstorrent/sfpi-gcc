// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// Named refusal: a consumed payload producer without an audited result
// latency (SFPLUTFP32, audit deliberately deferred) makes the window's
// reissue unpriceable; the record-hoist refuses by the same audit gate
// as the plain hoist, and the min-benefit override cannot force it.
// { dg-final { scan-rtl-dump "Not hoisting: replay-reissue-latency-unproved" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist refused: replay-reissue-latency-unproved" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
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
