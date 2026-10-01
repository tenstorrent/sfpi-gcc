// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// The raw `.ttinsn' face-advance pair (the LLK-pristine upstream TTI_
// shape): each canonical single-constant word is field-decoded
// architecturally (rvtt-raw-boundary.cc) as a pure Dst/RWC counter step
// -- the same run-separator class as the typed builtin -- so both
// eight-row runs share one materialized descriptor, exactly like the
// typed loadmacro-periodic-minmax-two-runs-face-bh.C; the raw words are
// preserved between the runs (2 more .ttinsn than the typed twin).
// Historically this test pinned the opposite: before the audited decode
// the raw word was opaque and poisoned the config-ownership proof (the
// pre-LLK-pristine "typed builtin is the only admitted form" doctrine).
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 37 } }
// { dg-final { scan-assembler-times "\\.ttinsn 923926532" 2 } }
// { dg-final { scan-assembler-times "SFPLOAD\\t" 16 } }
// { dg-final { scan-assembler-times "SFPNOP" 6 } }
// { dg-final { scan-assembler-not "TTSETRWC" } }
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

__attribute__((noinline)) void periodic_minmax_raw_face_word ()
{
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  asm volatile (".ttinsn %0" :: "n" (0x37120004));
  asm volatile (".ttinsn %0" :: "n" (0x37120004));
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
