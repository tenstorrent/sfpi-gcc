/* QSR's sfpsetcc and sfpswap carry an operand-type argument the other
   architectures do not have.  0 is IMM_TYPE_INT, which is what these
   bit-pattern subjects want.  Spelled as a token macro rather than an
   #if around the argument because some of these calls sit inside
   backslash-continued macro bodies.  */
#ifndef XTT_IMM_TYPE
#if __riscv_xtttensixqsr
#define XTT_IMM_TYPE , 0
#else
#define XTT_IMM_TYPE
#endif
#endif

/* Loop-backedge drain elision bodies: the compact-select
   (TTNN Where class) eight-row body inside a counted loop -- the shape
   whose final-run drain previously executed once per trip
   (macro-planner-select-compact-loop-bh.C's row, parameterized).
   DRAIN_LOOP_TAIL() lets the near-miss variants interpose a follower
   in the loop tail.  */

#ifndef SELECT_COND_ADDR
#define SELECT_COND_ADDR 0
#define SELECT_TRUE_ADDR 32
#define SELECT_FALSE_ADDR 64
#endif
#ifndef SELECT_SETCC_MOD
#define SELECT_SETCC_MOD 2
#endif
#ifndef SELECT_STRIDE
#define SELECT_STRIDE 2
#endif
#ifndef DRAIN_LOOP_TAIL
#define DRAIN_LOOP_TAIL()
#endif
#ifndef DRAIN_LOOP_NAME
#define DRAIN_LOOP_NAME select_faces_loop
#endif

#define ROW()                                                                 \
  do                                                                          \
    {                                                                         \
      auto condition = __builtin_rvtt_sfpload                                 \
	(nullptr, SELECT_COND_ADDR, 0, 0, 6, SELECT_ADDR_MODE);              \
      auto on_true = __builtin_rvtt_sfpload                                   \
	(nullptr, SELECT_TRUE_ADDR, 0, 0, 6, SELECT_ADDR_MODE);              \
      auto on_false = __builtin_rvtt_sfpload                                  \
	(nullptr, SELECT_FALSE_ADDR, 0, 0, 6, SELECT_ADDR_MODE);             \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfpsetcc (condition, SELECT_SETCC_MOD XTT_IMM_TYPE);                \
      auto result = __builtin_rvtt_sfpassign_lv (on_false, on_true);          \
      __builtin_rvtt_sfppopc (0);                                             \
      __builtin_rvtt_sfpstore (nullptr, result, SELECT_COND_ADDR, 0, 0, 6,    \
			       SELECT_ADDR_MODE);                            \
      __builtin_rvtt_ttincrwc (0, SELECT_STRIDE, 0, 0);                       \
    }                                                                         \
  while (0)

__attribute__((noinline)) void DRAIN_LOOP_NAME (unsigned faces)
{
  unsigned face = 0;
  do
    {
      ROW ();
      ROW ();
      ROW ();
      ROW ();
      ROW ();
      ROW ();
      ROW ();
      ROW ();
      DRAIN_LOOP_TAIL ();
    }
  while (++face < faces);
}
