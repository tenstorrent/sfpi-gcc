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

/* Renamed-equivalent, varied-constant twin of the in-place periodic
   minmax body (genericity obligation): every identifier is different,
   the Dst addresses differ (16/80, store in place to 16), and the
   selected result is the OTHER swap sense (min, not max).  The drain
   placement proof must key on the derived calendar facts alone, so it
   fires identically here.  */

#if __riscv_xtttensixwh
constexpr unsigned quiet_hold_mode = 3;
#else
constexpr unsigned quiet_hold_mode = 7;
#endif

#define QUIET_STEP()                                                          \
  do                                                                          \
    {                                                                         \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto left = __builtin_rvtt_sfpload                                      \
	(nullptr, 16, 0, 0, 0, quiet_hold_mode);                              \
      auto right = __builtin_rvtt_sfpload                                     \
	(nullptr, 80, 0, 0, 0, quiet_hold_mode);                              \
      auto both = __builtin_rvtt_sfpswap (left, right, 1 XTT_IMM_TYPE);                    \
      auto keep = __builtin_rvtt_sfpselect2 (both, 1);                        \
      __builtin_rvtt_sfpstore                                                 \
	(nullptr, keep, 0, 16, 0, 0, quiet_hold_mode);                        \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

#define QUIET_GROUP()                                                         \
  do                                                                          \
    {                                                                         \
      QUIET_STEP ();                                                          \
      QUIET_STEP ();                                                          \
      QUIET_STEP ();                                                          \
      QUIET_STEP ();                                                          \
      QUIET_STEP ();                                                          \
      QUIET_STEP ();                                                          \
      QUIET_STEP ();                                                          \
      QUIET_STEP ();                                                          \
    }                                                                         \
  while (0)

__attribute__((noinline)) void quiet_periodic_hold ()
{
  QUIET_GROUP ();
  __builtin_rvtt_ttdstface ();
  QUIET_GROUP ();
  __builtin_rvtt_ttdstface ();
  QUIET_GROUP ();
  __builtin_rvtt_ttdstface ();
  QUIET_GROUP ();
}

#undef QUIET_STEP
#undef QUIET_GROUP
