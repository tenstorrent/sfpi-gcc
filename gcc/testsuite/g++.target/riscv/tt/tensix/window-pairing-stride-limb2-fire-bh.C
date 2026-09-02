// Window-pairing stride-phase generalization: the limb-2
// MulInt32-class row hosts its store on the FIRST issued word (the
// launch that also absorbs the row's typed stride), so the established
// compact-absorber invariant (advance on the LAST issued word) refuses
// stride-unproven and keeps the full drain-2.  Under
// -mtt-tensix-optimize-window-pairing-stride the proof rebases every
// Dst footprint by its carrying word's stride phase (rvtt-cost.md F5':
// the absorber's own access resolves before ApplyPartialAddrMod;
// SFPLOADMACRO-hosted events latch their Dst row at the launch word)
// and the exact pending-event model then prices the boundary at ONE
// NOP: the binding blocker at zero spacing is the REAL fixed-VD WAR
// hazard (the pending hosted store reads the launch register the
// follower's first launch rewrites), named as the bound.  The replay
// window shrinks from five delivered words per row to four.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-window-pairing -mtt-tensix-optimize-window-pairing-stride -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=2 rows=8" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner window-pairing: interrow-drain 2 -> 1 rows=8 bound=window-pairing-lreg-overlap" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "window-pairing-refusal: window-pairing-stride-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 4, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 4, 0, 0" 7 } }
// { dg-final { scan-assembler-times "SFPNOP" 2 } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfpcast (a, 3);                                      \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      b = __builtin_rvtt_sfpcast (b, 3);                                      \
      auto hi = __builtin_rvtt_sfpmul24 (a, b, 1);                            \
      auto lo = __builtin_rvtt_sfpmul24 (a, b, 0);                            \
      hi = __builtin_rvtt_sfpshft_i (nullptr, hi, 23, 0, 0, 0);               \
      lo = __builtin_rvtt_sfpiadd_v (lo, hi, 4);                              \
      lo = __builtin_rvtt_sfpcast (lo, 3);                                    \
      __builtin_rvtt_sfpstore (nullptr, lo, 0, 0, 0, 4, 7);                   \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void limb2_product_rows ()
{
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
