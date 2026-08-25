/* In-place periodic min/max body: the row's SFPSTORE writes back to the
   SAME Dst address its first SFPLOAD read (the production kernel shape),
   unlike loadmacro-periodic-minmax-body.h whose store targets a distinct
   third address.  Under carrier grouping the same-address store first
   merges into the load's launch carrier; that candidate proves no
   descriptor program, and the planner's deterministic fallback demotes
   the store to its own delayed-store carrier (the frozen three-slot
   alternating-VD calendar).  */

#ifndef RESULT_INDEX
#define RESULT_INDEX 0
#endif
#ifndef MINMAX_INPLACE_LINKAGE
#define MINMAX_INPLACE_LINKAGE
#endif

#if __riscv_xtttensixwh
constexpr unsigned minmax_no_increment = 3;
#else
constexpr unsigned minmax_no_increment = 7;
#endif

#define MINMAX_ROW()                                                          \
  do                                                                          \
    {                                                                         \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload                                         \
	(nullptr, 0, 0, 0, 0, minmax_no_increment);                         \
      auto b = __builtin_rvtt_sfpload                                         \
	(nullptr, 64, 0, 0, 0, minmax_no_increment);                        \
      auto pair = __builtin_rvtt_sfpswap (a, b, 1);                           \
      auto result = __builtin_rvtt_sfpselect2 (pair, RESULT_INDEX);            \
      __builtin_rvtt_sfpstore                                                 \
	(nullptr, result, 0, 0, 0, 0, minmax_no_increment);                  \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

#define MINMAX_EIGHT_ROWS()                                                   \
  do                                                                          \
    {                                                                         \
      MINMAX_ROW ();                                                          \
      MINMAX_ROW ();                                                          \
      MINMAX_ROW ();                                                          \
      MINMAX_ROW ();                                                          \
      MINMAX_ROW ();                                                          \
      MINMAX_ROW ();                                                          \
      MINMAX_ROW ();                                                          \
      MINMAX_ROW ();                                                          \
    }                                                                         \
  while (0)

/* The face advance between runs: the typed builtin by default; the
   rawword test variants override it with the LLK-pristine upstream shape
   (two canonical raw `.ttinsn %0' SETRWC words).  */
#ifndef MINMAX_FACE_ADVANCE
#define MINMAX_FACE_ADVANCE() __builtin_rvtt_ttdstface ()
#endif

__attribute__((noinline)) MINMAX_INPLACE_LINKAGE void periodic_minmax_inplace ()
{
#ifdef MINMAX_FOUR_FACE_RUNS
  /* The production shape: four eight-row face runs (rows=32 runs=4)
     separated by the architectural face advance.  */
  MINMAX_EIGHT_ROWS ();
  MINMAX_FACE_ADVANCE ();
  MINMAX_EIGHT_ROWS ();
  MINMAX_FACE_ADVANCE ();
  MINMAX_EIGHT_ROWS ();
  MINMAX_FACE_ADVANCE ();
  MINMAX_EIGHT_ROWS ();
#else
  MINMAX_EIGHT_ROWS ();
#endif
}

#undef MINMAX_ROW
#undef MINMAX_EIGHT_ROWS
