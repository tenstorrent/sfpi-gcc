// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Hoist pricing .loop \\d+.: trips 4, words 8, exec_ilk 15 slots .re-record body, execution-bound., deliver_body 984, deliver_record 1107, record 300, before 1800, after 1570, benefit 620 .min 60." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoist profitable: modeled benefit 620 >= 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoist pricing .loop \\d+.: trips 4, words 8, exec_ilk 8 slots .re-record body, delivery-bound., deliver_body 984, deliver_record 1107, record 1407, before 1107, after 870, benefit -459 .min 60." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -459 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoist profitable: modeled benefit 390 >= 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 2 "rvtt_replay" } }

// EXECUTION-bound re-record hoists fire; the same-length delivery-bound
// twin refuses.  This is the Reduce-class hardware anchor structure
// (855.5 -> 832.75 cyc/body hardware A/B; two
// preheader hoists of a 4-trip, 8-word, interlock-stalled payload with
// sibling launches): when the payload's interlocked reissue is at least
// its own record delivery (exec >= deliver_record), the in-loop
// record-with-execution pass exposes the record engine's per-pass
// overhead on the critical path, and the hoisted preheader pass's
// delivery hides behind the loop's execution backlog.
//
// execbound_fires: serially-chained eight-op mad-family payload (result latency 1;
// square-free op pattern so the sequence former cannot re-segment)
// interlocks to 15 slots: exec = 1500 >=
// deliver_record 1107.  before = 1500 + 300, after = 1500 + 70,
// benefit = 4 * 230 - 300 = 620 >= 60: FIRE.
void execbound_fires ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpadd (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

// deliverybound_refuses: the discriminating twin -- same trips, same
// word count, but four rotating accumulators (every dependence four
// slots apart, zero modeled interlock stalls; one op varied between
// rounds so no shorter period exists): exec = 800 <
// deliver_record 1107, so the delivery-bound arithmetic decides:
// 4 * (1107 - 870) - 1407 = -459 < 60: REFUSE.  The branch keys on the
// audited interlock structure of the payload, never on an operation
// identity.
void deliverybound_refuses ()
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
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpadd (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpmul (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      a = __builtin_rvtt_sfpmul (a, a, 0);
      b = __builtin_rvtt_sfpmul (b, b, 0);
      c = __builtin_rvtt_sfpadd (c, c, 0);
      d = __builtin_rvtt_sfpmul (d, d, 0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
  __builtin_rvtt_sfpwritelreg (d, 3);
}

// Unrelated second execution-bound shape (different length, different
// trip count): a ten-op serial add chain (one mul breaking the period)
// interlocks to 19 slots, exec =
// 1900 >= deliver_record 1353; benefit = 3 * 230 - 300 = 390 >= 60:
// FIRE.
void execbound_second_shape ()
{
  auto y = __builtin_rvtt_sfpreadlreg (5);
  for (unsigned ix = 0; ix != 3; ++ix)
    {
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpmul (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      y = __builtin_rvtt_sfpadd (y, y, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (y, 5);
}
