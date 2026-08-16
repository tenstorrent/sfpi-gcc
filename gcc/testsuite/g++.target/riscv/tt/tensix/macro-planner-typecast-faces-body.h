/* Typecast four-face shape (WP8 step 4): eight cast-round rows per
   face, typed TTSETRWC face transitions, one all-lanes proof before
   the faces.  The planner must share ONE descriptor configuration
   across all four typed-TTSETRWC-separated face regions.  */
#if __riscv_xtttensixwh
constexpr unsigned no_increment = 3;
#else
constexpr unsigned no_increment = 7;
#endif

#define ROW()                                                                 \
  do                                                                          \
    {                                                                         \
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6,              \
					    no_increment);                    \
      auto cast = __builtin_rvtt_sfpcast (loaded, 0);                         \
      auto rounded                                                            \
	= __builtin_rvtt_sfpstochrnd_i (nullptr, cast, 0, 0, 0, 1, 0);        \
      __builtin_rvtt_sfpstore (nullptr, rounded, 0, 0, 0, 2,                  \
			       no_increment);                                 \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

#define FACE()                                                                \
  do                                                                          \
    {                                                                         \
      ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();         \
      __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 4);                             \
      __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 4);                             \
    }                                                                         \
  while (0)
