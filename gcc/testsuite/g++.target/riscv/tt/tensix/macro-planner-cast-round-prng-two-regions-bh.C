// Check that an opaque entropy consumer splits two cast-round regions and only the first forms.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times {Macro-planner region: rows=8} 2 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump {formation-refusal: config-ownership-unproven} "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times {SFPSTOCHRND} 9 } }
// { dg-final { scan-assembler-times {\.ttinsn[ \t]+2466693120} 4 } }
// { dg-final { scan-assembler-times {\.ttinsn[ \t]+2467741696} 4 } }
// { dg-final { scan-assembler {\.ttinsn[ \t]+2467741696(.|\n)*SFPSTOCHRND(.|\n)*SFPSTOCHRND} } }

#define ROW() do {                                                     \
  auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);     \
  auto cast = __builtin_rvtt_sfpcast (loaded, 0);                     \
  auto rounded = __builtin_rvtt_sfpstochrnd (cast, 1, 0);             \
  __builtin_rvtt_sfpstore (nullptr, rounded, 0, 0, 0, 2, 7);          \
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);                               \
} while (0)

__attribute__((noinline)) void cast_round_prng_two_regions ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();

  auto middle = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  auto middle_round = __builtin_rvtt_sfpstochrnd_descale_i
    (nullptr, middle, 3, 0, 0, 4, 0);
  __builtin_rvtt_sfpstore (nullptr, middle_round, 64, 0, 0, 2, 7);

  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
