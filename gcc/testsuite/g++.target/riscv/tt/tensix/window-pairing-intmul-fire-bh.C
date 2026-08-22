// Lane FT window-pairing (inter-row drain tuning): the MulInt32-class
// derived calendar pins every value carrier (lane EV's inter-row
// obligation places the full drain-2 between rows).  The exact
// pending-event model proves the boundary at ZERO spacing: the only
// pending event is the store carrier's delayed store (reads the
// store-value register, writes its launch-latched Dst row); the next
// row's first launch writes a DIFFERENT register, and its Dst read
// sits in the same four-row group at the OPPOSITE column parity
// (stride 2 flips Addr bit 1; SFPLOAD.md/SFPSTORE.md column formula
// under the proven-default LaneConfig).  Rows return to back-to-back
// -- the paired replay window shape -- and only the run-end exit
// drain remains.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-window-pairing -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=2 rows=8" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner window-pairing: interrow-drain 2 -> 0 rows=8" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466570240" 8 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2471813184" 8 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2477047808" 8 } }

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
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
}

#undef ROW
