#ifndef RESULT_INDEX
#define RESULT_INDEX 0
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
	(nullptr, result, 128, 0, 0, 0, minmax_no_increment);                \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void periodic_minmax ()
{
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
#ifndef MINMAX_SHORT_BODY
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
#ifndef MINMAX_SIX_BODY
  MINMAX_ROW ();
#ifndef MINMAX_SEVEN_BODY
  MINMAX_ROW ();
#endif
#endif
#endif
}

#undef MINMAX_ROW
