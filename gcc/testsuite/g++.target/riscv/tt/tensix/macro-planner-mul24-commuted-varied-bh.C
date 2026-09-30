// Renamed/varied twin of the commuted-SFPMUL24 fire: different symbol
// names, Dst offsets, slack-shift immediates, and the UPPER (mod 1)
// product half.  The commuted admission carries the mod through
// unchanged, so the derived word differs from the fire twin's exactly
// in the mod nibble and the calendar still forms and verifies.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=1: 0x980109d1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPMUL24" } }

#define WIDE_ROW()                                                            \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto left = __builtin_rvtt_sfpload (nullptr, 32, 0, 0, 4, 7);           \
      left = __builtin_rvtt_sfpcast (left, 3);                                \
      auto right = __builtin_rvtt_sfpload (nullptr, 96, 0, 0, 4, 7);          \
      right = __builtin_rvtt_sfpcast (right, 3);                              \
      right = __builtin_rvtt_sfpmul24 (right, left, 1);                       \
      auto scaled = __builtin_rvtt_sfpshft_i (nullptr, left, 3, 0, 0, 0);     \
      scaled = __builtin_rvtt_sfpshft_i (nullptr, scaled, 4, 0, 0, 0);        \
      right = __builtin_rvtt_sfpiadd_v (right, scaled, 4);                    \
      right = __builtin_rvtt_sfpcast (right, 3);                              \
      __builtin_rvtt_sfpstore (nullptr, right, 0, 0, 0, 4, 7);                \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void varied_commuted_rows ()
{
  WIDE_ROW (); WIDE_ROW (); WIDE_ROW (); WIDE_ROW ();
  WIDE_ROW (); WIDE_ROW (); WIDE_ROW (); WIDE_ROW ();
}
#undef WIDE_ROW

int main ()
{
  varied_commuted_rows ();
  return 0;
}
