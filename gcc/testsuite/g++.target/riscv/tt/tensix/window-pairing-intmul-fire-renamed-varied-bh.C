// Window-pairing, renamed-varied variant of the MulInt32-class
// fire: different symbol names, Dst bases (128/192), and row count (6).
// The verdict is mechanism-keyed, not shape-keyed: same tune to zero.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-window-pairing -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=2 rows=6" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner window-pairing: interrow-drain 2 -> 0 rows=6" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }

#define WIDE_STEP()                                                           \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto lhs = __builtin_rvtt_sfpload (nullptr, 128, 0, 0, 4, 7);           \
      lhs = __builtin_rvtt_sfpcast (lhs, 3);                                  \
      auto rhs = __builtin_rvtt_sfpload (nullptr, 192, 0, 0, 4, 7);           \
      rhs = __builtin_rvtt_sfpcast (rhs, 3);                                  \
      auto low = __builtin_rvtt_sfpmul24 (lhs, rhs, 0);                       \
      auto high = __builtin_rvtt_sfpmul24 (lhs, rhs, 1);                      \
      auto lsh = __builtin_rvtt_sfpshft_i (nullptr, lhs, -23, 0, 0, 0);       \
      auto part = __builtin_rvtt_sfpmul24 (lsh, rhs, 0);                      \
      rhs = __builtin_rvtt_sfpshft_i (nullptr, rhs, -23, 0, 0, 0);            \
      high = __builtin_rvtt_sfpiadd_v (high, part, 4);                        \
      rhs = __builtin_rvtt_sfpmul24 (lhs, rhs, 0);                            \
      high = __builtin_rvtt_sfpiadd_v (high, rhs, 4);                         \
      high = __builtin_rvtt_sfpshft_i (nullptr, high, 23, 0, 0, 0);           \
      low = __builtin_rvtt_sfpiadd_v (low, high, 4);                          \
      low = __builtin_rvtt_sfpcast (low, 3);                                  \
      __builtin_rvtt_sfpstore (nullptr, low, 0, 0, 128, 4, 7);                \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void renamed_wide_product ()
{
  WIDE_STEP ();
  WIDE_STEP ();
  WIDE_STEP ();
  WIDE_STEP ();
  WIDE_STEP ();
  WIDE_STEP ();
}

#undef WIDE_STEP
