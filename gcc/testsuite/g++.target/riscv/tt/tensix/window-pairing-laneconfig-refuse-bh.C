#include "rtl-lane-reset.h"

// Nonidentity or unknown LaneConfig must not borrow the entry default.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-window-pairing -fdump-rtl-rvtt_macro_planner" }
// The earlier physical-lane gate refuses before a paired calendar exists.
// { dg-final { scan-rtl-dump-times "Macro-planner formation-refusal: cc-enable-unproved" 3 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed:" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner window-pairing:" "rvtt_macro_planner" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);               \
      a = __builtin_rvtt_sfpcast (a, 3);                                      \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 4, 7);              \
      b = __builtin_rvtt_sfpcast (b, 3);                                      \
      auto lo = __builtin_rvtt_sfpmul24 (a, b, 0);                            \
      auto hi = __builtin_rvtt_sfpmul24 (a, b, 1);                            \
      auto sa = __builtin_rvtt_sfpshft_i (nullptr, a, -23, 0, 0, 0);          \
      auto c0 = __builtin_rvtt_sfpmul24 (sa, b, 0);                           \
      b = __builtin_rvtt_sfpshft_i (nullptr, b, -23, 0, 0, 0);                \
      hi = __builtin_rvtt_sfpiadd_v (hi, c0, 4);                              \
      b = __builtin_rvtt_sfpmul24 (a, b, 0);                                  \
      hi = __builtin_rvtt_sfpiadd_v (hi, b, 4);                               \
      hi = __builtin_rvtt_sfpshft_i (nullptr, hi, 23, 0, 0, 0);               \
      lo = __builtin_rvtt_sfpiadd_v (lo, hi, 4);                              \
      lo = __builtin_rvtt_sfpcast (lo, 3);                                    \
      __builtin_rvtt_sfpstore (nullptr, lo, 0, 0, 0, 4, 7);                   \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void wide_int_product_rows ()
{
  RVTT_TEST_LANE_RESET ();
  __builtin_rvtt_sfpconfig_i (1, 15, 1);
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
}

__attribute__((noinline)) void wide_int_unknown_laneconfig ()
{
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}

__attribute__((noinline)) void wide_int_flags_only ()
{
  __builtin_rvtt_sfpencc_all_lanes ();
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}

#undef ROW
