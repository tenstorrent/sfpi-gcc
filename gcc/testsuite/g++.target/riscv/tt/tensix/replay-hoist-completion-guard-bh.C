// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-completion-guard -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump "Replay completion guard: execution-bound re-record charges hoisted delivery 1107 .record cost 1407." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoist pricing .loop \\d+.: trips 4, words 8, exec_ilk 15 slots .re-record body, execution-bound., deliver_body 984, deliver_record 1107, record 1407, before 1800, after 1570, benefit -487 .min 60." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -487 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoist pricing .loop \\d+.: trips 4, words 8, exec_ilk 8 slots .re-record body, delivery-bound., deliver_body 984, deliver_record 1107, record 1407, before 1107, after 870, benefit -459 .min 60." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -459 < 60" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }

// Generic execution-bound witness: eight dependent words, four trips.
// The ordinary body-throughput model fires this shape at +620.  Completion
// pricing charges the otherwise-hidden 1107-centislot preheader delivery and
// conservatively refuses at -487.
void completion_refuses_execbound ()
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

// Same length/trips, but independent rotating accumulators make the existing
// delivery-bound model refuse.  The completion guard must not perturb that
// branch or introduce a kernel-specific shape check.
void completion_deliverybound_nearmiss ()
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
