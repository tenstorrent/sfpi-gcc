/* Unary load/shift/cast/store loop row (the signbit kernel shape): one
   row per trip, typed Dst += 2, all-lanes proof in the body, unknown
   trip count behind a zero-trip guard.  */
#if __riscv_xtttensixwh
constexpr unsigned no_increment = 3;
#else
constexpr unsigned no_increment = 7;
#endif

#ifndef STAGED_SHIFT
#define STAGED_SHIFT -31
#endif

__attribute__((noinline)) void staged_loop (unsigned iterations)
{
  for (unsigned row = 0; row < iterations; ++row)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0,
					    no_increment);
      auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded,
					       STAGED_SHIFT, 0, 0, 0);
      auto converted = __builtin_rvtt_sfpcast (shifted, 0);
      __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0,
			       no_increment);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
