// Lane EV: the WH mirror of macro-planner-interrow-drain-bh.C -- the WH
// capability tables carry the same fixed-VD unary shift/cast program,
// so the inter-row drain applies identically (WH no-increment address
// mode is 3).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=3 rows=8" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 24 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 11 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-not "SFPSHFT" } }
// { dg-final { scan-assembler-not "SFPCAST" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 3);          \
      auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);\
      auto converted = __builtin_rvtt_sfpcast (shifted, 0);                   \
      __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 3);            \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void signbit_rows ()
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
