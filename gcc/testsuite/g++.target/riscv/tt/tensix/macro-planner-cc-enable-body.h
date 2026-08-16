/* Shared eight-row Min/Max body for the ambient-enable proof corpus
   (P0/D1: the planner may accept a CC write as the ambient all-lanes
   enable ONLY when the written value provably enables all lanes).

   The single enable statement ahead of the rows is the parameter:
   CC_ENABLE_STMT defaults to the proven all-lanes shape (pushc/popc,
   whose outermost popc the CC pass replaces with the synthesized
   all-lanes SFPENCC); refusal twins override it with unproved pure CC
   writes.  CC_ENABLE_FN parameterizes the function name so
   renamed-equivalent twins prove name-independence.  The header is
   re-includable: every parameter macro is consumed and #undef'd, so
   one test file can instantiate several probes.  */

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
      auto pair = __builtin_rvtt_sfpswap (a, b, 1);                           \
      auto result = __builtin_rvtt_sfpselect2 (pair, 0);                      \
      __builtin_rvtt_sfpstore                                                 \
	(nullptr, result, CC_ENABLE_STORE_ADDR, 0, 0, 0,                      \
	 cc_enable_no_increment);                                             \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void CC_ENABLE_FN ()
{
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
