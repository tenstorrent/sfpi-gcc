// Stage-A dual-oracle trip facade (FABLE_GOES_BURR item #2): on a known
// counted loop BOTH oracles -- the legacy bounded simulation and the
// classical SCEV niter analysis -- prove at the GIMPLE face and agree
// (4 body executions), dumped as trip-oracle-agree.  The RTL face pins
// the banked stage-A finding: post-reload loop-iv refuses hard-register
// counters (simple_reg_p), so the RTL classical oracle proves nothing
// and the query dumps trip-oracle-legacy-only.  The legacy oracle
// decides throughout: the hoist fires exactly as before the facade
// (one record + eight launches).
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-delivery-shape -fdump-tree-rvtt_delivery_shape -fdump-rtl-rvtt_replay" }
// { dg-final { scan-tree-dump "rvtt-trips: trip-oracle-agree loop \\d+ n=4 \\(gimple\\)" "rvtt_delivery_shape" } }
// { dg-final { scan-tree-dump-not "trip-oracle-divergence" "rvtt_delivery_shape" } }
// { dg-final { scan-rtl-dump "rvtt-trips: trip-oracle-legacy-only loop \\d+ n=4 \\(rtl\\)" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "trip-oracle-divergence" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 9 } }

void hoist ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 4; ++ix)
    {
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (x, x, 0);
      __builtin_rvtt_ttincrwc (0, 4, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
