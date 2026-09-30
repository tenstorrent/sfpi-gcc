// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay-details" }
// Record-hoist fire: a proven-trip single-block loop re-records an
// iteration-invariant 6-word capture window (two clones separated by a
// volatile scalar store, so the body is NOT a counted-loop payload and
// the re-record sequence path owns it).  Under the flag the record
// phase hoists to the dedicated preheader as a no-exec capture and the
// body keeps one playback launch per former clone.  Issue-side pricing:
// 4 trips x (6 words x 123 - 70 boundary) - (7 x 123 + 300) = 1511 >= 60.
// { dg-final { scan-rtl-dump "record-hoist: invariant re-record window admitted \\(trips 4, words 6, benefit 1511\\)" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 0" 2 } }
void rerecord_fire (volatile int *out)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
      *out = (int) ix;	// volatile scalar separator: not a counted-loop payload
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      a = __builtin_rvtt_sfpmul (a, b, 0);
      b = __builtin_rvtt_sfpmul (b, c, 0);
      c = __builtin_rvtt_sfpmul (c, a, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
