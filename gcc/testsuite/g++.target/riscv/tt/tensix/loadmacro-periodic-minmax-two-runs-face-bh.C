// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// Two eight-row runs separated only by the typed architectural face advance
// share one materialized descriptor; the separator is recognized by typed
// identity (a pure Dst/RWC effect), preserved between the runs, and never
// re-derived into source offsets.
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 35 } }
// { dg-final { scan-assembler-times "SFPLOAD\\t" 16 } }
// { dg-final { scan-assembler-times "SFPNOP" 6 } }
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 2 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }

#define MINMAX_ROW()                                                          \
  do                                                                          \
    {                                                                         \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);               \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);              \
      auto pair = __builtin_rvtt_sfpswap (a, b, 1);                           \
      auto result = __builtin_rvtt_sfpselect2 (pair, 0);                      \
      __builtin_rvtt_sfpstore (nullptr, result, 128, 0, 0, 0, 7);             \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void periodic_minmax_two_runs_face ()
{
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  __builtin_rvtt_ttdstface ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
}

#undef MINMAX_ROW
