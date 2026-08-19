// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Record delivery hidden: contiguous launch run 8 exec surplus 2216 >= record delivery 615" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -915 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }

// Execution-saturated sibling-launch shape, DELIVERY-bound payload (the
// silicon-witnessed unary-max/min hoist LOSS class): a four-trip loop
// whose body is eight sibling copies of a four-slot payload of four
// rotating accumulators (every dependence four slots apart, zero
// modeled interlock stalls: exec = 4 * 100 = 400 < deliver_record
// 5 * 123 = 615, so the record pass is delivery-bound), each copy
// followed only by a typed Dst increment that the enabled Dst
// auto-increment pass later absorbs around the launches.  The
// final-stream launch run is therefore 8; its execution surplus
// 8 * (400 - 123) = 2216 centislots hides the in-loop record pass's
// 615-centislot delivery, so hoisting relieves nothing per trip and
// would only buy the record-only preheader pass: modeled benefit
// 4 * 0 - (615 + 300) = -915 < 60.  The capture must stay in the loop
// as an ordinary record-with-execution, byte-identical to the
// unhoisted form.  (The delivery-only model priced this shape +245 and
// fired; silicon measured the class +3.93 cyc/tile.)  The saturation
// term applies only here, in the delivery-bound branch: an
// execution-bound re-record payload is priced by
// replay-hoist-rerecord-execbound-fire-bh.C instead.
void saturated_siblings ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto d = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}
