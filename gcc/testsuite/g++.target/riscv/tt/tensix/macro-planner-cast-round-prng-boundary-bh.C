// Check that a cast-round calendar keeps the two surrounding PRNG consumers in place.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times {SFPSTOCHRND} 2 } }
// { dg-final { scan-assembler-times {SFPCONFIG} 4 } }
// { dg-final { scan-assembler-times {\.ttinsn[ \t]+2466693120} 4 } }
// { dg-final { scan-assembler-times {\.ttinsn[ \t]+2467741696} 4 } }
// { dg-final { scan-assembler {SFPSTOCHRND(.|\n)*SFPCONFIG(.|\n)*SFPSTOCHRND} } }
// { dg-final { scan-assembler-not {SFPCAST} } }

#define ROW() do {                                                     \
  auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);     \
  auto cast = __builtin_rvtt_sfpcast (loaded, 0);                     \
  auto rounded = __builtin_rvtt_sfpstochrnd (cast, 1, 0);             \
  __builtin_rvtt_sfpstore (nullptr, rounded, 0, 0, 0, 2, 7);          \
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);                               \
} while (0)

__attribute__((noinline)) void cast_round_prng_boundaries ()
{
  auto before = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto before_round = __builtin_rvtt_sfpstochrnd_descale_i
    (nullptr, before, 3, 0, 0, 4, 0);
  __builtin_rvtt_sfpstore (nullptr, before_round, 0, 0, 0, 2, 7);

  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();

  auto after = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  auto after_round = __builtin_rvtt_sfpstochrnd_descale_i
    (nullptr, after, 3, 0, 0, 4, 0);
  __builtin_rvtt_sfpstore (nullptr, after_round, 64, 0, 0, 2, 7);
}
