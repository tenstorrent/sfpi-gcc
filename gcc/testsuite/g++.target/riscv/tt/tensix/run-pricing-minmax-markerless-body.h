/* Init-hoist-aware run pricing bodies (lane IU, 2026-08-29): the
   POST-F1 production minmax shape -- MARKER-FREE rows (the deleted
   sfppushc(0)/sfppopc(0) pair; the planner derives the entry-ambient
   all-lanes enable), four eight-row face runs (rows=32 runs=4) in a
   noinline per-tile callee, called from a counted caller loop whose
   prelude seeds the reaching configuration (the stage-2
   value-equality subject).  Without the marker the explicit side
   prices at its honest 5 words/row (4 row words + separator), which
   the frozen conservative-per-run discipline cannot amortize the
   configuration prefix against -- the production `unprofitable'
   refusal.  The caller-loop amortization (rvtt-cost.md
   INIT-HOIST-AWARE RUN PRICING) re-admits it under the proven
   stage-2 contract.

   RP_ROW_RESULT selects max (1) or min (0) -- the varied twin's knob.
   RP_CALLEE_NAME / RP_CALLER_NAME rename the pair.
   RP_SECOND_CALLER, when defined, adds a SECOND call site: the
   init-hoist closure refuses (multi-site) and the frozen pricing
   must hold -- the refusal returns.  */

#ifndef RP_ROW_RESULT
#define RP_ROW_RESULT 1
#endif
#ifndef RP_CALLEE_NAME
#define RP_CALLEE_NAME periodic_minmax_markerless
#endif
#ifndef RP_CALLER_NAME
#define RP_CALLER_NAME caller_tiles
#endif

#define RP_ROW()                                                              \
  do                                                                          \
    {                                                                         \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);              \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);             \
      auto pair = __builtin_rvtt_sfpswap (a, b, 1);                          \
      auto result = __builtin_rvtt_sfpselect2 (pair, RP_ROW_RESULT);         \
      __builtin_rvtt_sfpstore (nullptr, result, 0, 0, 0, 0, 7);             \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                  \
    }                                                                         \
  while (0)

#define RP_EIGHT_ROWS()                                                       \
  do                                                                          \
    {                                                                         \
      RP_ROW ();                                                              \
      RP_ROW ();                                                              \
      RP_ROW ();                                                              \
      RP_ROW ();                                                              \
      RP_ROW ();                                                              \
      RP_ROW ();                                                              \
      RP_ROW ();                                                              \
      RP_ROW ();                                                              \
    }                                                                         \
  while (0)

__attribute__((noinline)) void RP_CALLEE_NAME ()
{
  RP_EIGHT_ROWS ();
  __builtin_rvtt_ttdstface ();
  RP_EIGHT_ROWS ();
  __builtin_rvtt_ttdstface ();
  RP_EIGHT_ROWS ();
  __builtin_rvtt_ttdstface ();
  RP_EIGHT_ROWS ();
}

__attribute__((noinline)) void RP_CALLER_NAME (unsigned tiles)
{
  asm volatile (".ttinsn %0" :: "n" (0xb2120000u));
  asm volatile (".ttinsn %0" :: "n" (0xb2220002u));
  asm volatile (".ttinsn %0" :: "n" (0xb2350000u));
  unsigned t = 0;
  do
    {
      RP_CALLEE_NAME ();
      __builtin_rvtt_ttdstface ();
    }
  while (++t < tiles);
}

#ifdef RP_SECOND_CALLER
__attribute__((noinline)) void RP_SECOND_CALLER (unsigned tiles)
{
  unsigned t = 0;
  do
    {
      RP_CALLEE_NAME ();
      __builtin_rvtt_ttdstface ();
    }
  while (++t < tiles);
}
#endif

#undef RP_ROW
#undef RP_EIGHT_ROWS
