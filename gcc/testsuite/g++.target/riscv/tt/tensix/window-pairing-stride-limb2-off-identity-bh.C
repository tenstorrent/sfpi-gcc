// Window-pairing stride-phase generalization, FLAG-OFF control:
// the same limb-2 MulInt32-class row (store hosted on the FIRST issued
// word, the stride-absorbing launch) WITHOUT
// -mtt-tensix-optimize-window-pairing-stride keeps the lane-FT
// compact-absorber refusal byte-identically: the tuner refuses
// window-pairing-stride-unproven by name, no tune line is printed, and
// the emitted stream keeps the full drain-2 (five delivered words per
// replay row).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-window-pairing -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=2 rows=8" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner window-pairing-refusal: window-pairing-stride-unproven" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner window-pairing: interrow-drain" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 5, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 5, 0, 0" 7 } }
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
