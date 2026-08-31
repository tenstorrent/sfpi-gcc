// Stage-A dual-oracle trip facade (FABLE_GOES_BURR item #2): manufactured
// near-miss for the trip-oracle-divergence diagnostic.  The testing-only
// skew knob shifts a proven classical verdict by one trip, so on the same
// counted loop as trips-facade-agree-bh.C the oracles now disagree at the
// GIMPLE face (legacy=4, classical=5): the divergence must dump by name,
// and the LEGACY verdict must still decide -- the hoist fires with the
// identical delivery shape (one record + eight launches).
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-delivery-shape -mtt-tensix-trips-oracle-skew=1 -fdump-tree-rvtt_delivery_shape -fdump-rtl-rvtt_replay" }
// { dg-final { scan-tree-dump "rvtt-trips: trip-oracle-divergence loop \\d+ legacy=4 classical=5 \\(gimple\\) -- legacy verdict kept" "rvtt_delivery_shape" } }
// { dg-final { scan-tree-dump-not "trip-oracle-agree" "rvtt_delivery_shape" } }
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
