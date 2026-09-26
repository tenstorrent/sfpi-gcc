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

/* Shared body for the periodic min/max near-miss refusal corpus.  The
   defaults reproduce the exact formable eight-row shape; each near-miss test
   perturbs exactly one parameter and must refuse byte-identically to the
   default pipeline (OFF/ON .text oracles live in the refusal-identity
   store).  */

#ifndef MINMAX_LOAD0_ADDR
#define MINMAX_LOAD0_ADDR 0
#endif
#ifndef MINMAX_LOAD1_ADDR
#define MINMAX_LOAD1_ADDR 64
#endif
#ifndef MINMAX_STORE_ADDR
#define MINMAX_STORE_ADDR 128
#endif
#ifndef MINMAX_LOAD1_MODE
#define MINMAX_LOAD1_MODE 0
#endif
#ifndef MINMAX_SWAP_MOD
#define MINMAX_SWAP_MOD 1
#endif
#ifndef MINMAX_INC_D
#define MINMAX_INC_D 2
#endif

#if __riscv_xtttensixwh
constexpr unsigned minmax_no_increment = 3;
#else
constexpr unsigned minmax_no_increment = 7;
#endif

#ifdef MINMAX_OMIT_ENABLE
#define MINMAX_ENABLE() do { } while (0)
#else
#define MINMAX_ENABLE()                                                       \
  do                                                                          \
    {                                                                         \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
    }                                                                         \
  while (0)
#endif

#ifdef MINMAX_OMIT_INCRWC
#define MINMAX_INCRWC() do { } while (0)
#else
#define MINMAX_INCRWC() __builtin_rvtt_ttincrwc (0, MINMAX_INC_D, 0, 0)
#endif

#define MINMAX_ROW()                                                          \
  do                                                                          \
    {                                                                         \
      MINMAX_ENABLE ();                                                       \
      auto a = __builtin_rvtt_sfpload                                         \
	(nullptr, MINMAX_LOAD0_ADDR, 0, 0, 0, minmax_no_increment);          \
      auto b = __builtin_rvtt_sfpload                                         \
	(nullptr, MINMAX_LOAD1_ADDR, 0, 0, MINMAX_LOAD1_MODE,                \
	 minmax_no_increment);                                                \
      auto pair = __builtin_rvtt_sfpswap (a, b, MINMAX_SWAP_MOD XTT_IMM_TYPE);             \
      auto result = __builtin_rvtt_sfpselect2 (pair, 0);                      \
      __builtin_rvtt_sfpstore                                                 \
	(nullptr, result, MINMAX_STORE_ADDR, 0, 0, 0, minmax_no_increment);  \
      MINMAX_INCRWC ();                                                       \
    }                                                                         \
  while (0)

__attribute__((noinline)) void periodic_minmax_near_miss ()
{
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
#undef MINMAX_ENABLE
#undef MINMAX_INCRWC
