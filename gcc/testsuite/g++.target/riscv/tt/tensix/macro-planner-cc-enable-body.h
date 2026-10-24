#ifndef RVTT_TEST_LANE_RESET
#define RVTT_TEST_LANE_RESET() ((void) 0)
#endif

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

/* Eight-row Min/Max body preceded by the enable statement CC_ENABLE_STMT
   (default: the proven pushc/popc all-lanes shape) in function CC_ENABLE_FN.
   Re-includable: every parameter macro is #undef'd after use.  */

#ifndef CC_ENABLE_BODY_COMMON
#define CC_ENABLE_BODY_COMMON
#if __riscv_xtttensixwh
constexpr unsigned cc_enable_no_increment = 3;
#else
constexpr unsigned cc_enable_no_increment = 7;
#endif
#endif

#ifndef CC_ENABLE_FN
#define CC_ENABLE_FN cc_enable_minmax
#endif
#ifndef CC_ENABLE_STMT
#define CC_ENABLE_STMT                                                        \
  do                                                                          \
    {                                                                         \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
    }                                                                         \
  while (0)
#endif
#ifndef CC_ENABLE_LOAD1_ADDR
#define CC_ENABLE_LOAD1_ADDR 64
#endif
#ifndef CC_ENABLE_STORE_ADDR
#define CC_ENABLE_STORE_ADDR 128
#endif

#define CC_ENABLE_ROW()                                                       \
  do                                                                          \
    {                                                                         \
      auto a = __builtin_rvtt_sfpload                                         \
	(nullptr, 0, 0, 0, 0, cc_enable_no_increment);                        \
      auto b = __builtin_rvtt_sfpload                                         \
	(nullptr, CC_ENABLE_LOAD1_ADDR, 0, 0, 0, cc_enable_no_increment);     \
      auto pair = __builtin_rvtt_sfpswap (a, b, 1 XTT_IMM_TYPE);                           \
      auto result = __builtin_rvtt_sfpselect2 (pair, 0);                      \
      __builtin_rvtt_sfpstore                                                 \
	(nullptr, result, CC_ENABLE_STORE_ADDR, 0, 0, 0,                      \
	 cc_enable_no_increment);                                             \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void CC_ENABLE_FN ()
{
  RVTT_TEST_LANE_RESET ();
  CC_ENABLE_STMT;
  CC_ENABLE_ROW ();
  CC_ENABLE_ROW ();
  CC_ENABLE_ROW ();
  CC_ENABLE_ROW ();
  CC_ENABLE_ROW ();
  CC_ENABLE_ROW ();
  CC_ENABLE_ROW ();
  CC_ENABLE_ROW ();
}

#undef CC_ENABLE_ROW
#undef CC_ENABLE_FN
#undef CC_ENABLE_STMT
#undef CC_ENABLE_LOAD1_ADDR
#undef CC_ENABLE_STORE_ADDR
