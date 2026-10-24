#include "rtl-lane-reset.h"

// Check that a loop that may run zero times refuses to hoist the all-lanes enable.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "zero-trip-preheader-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not {SFPCONFIG[ \t]+([0-9]|1[0-4]),} } }
// { dg-final { scan-assembler-times "SFPSHFT" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 1 } }

#if __riscv_xtttensixwh
constexpr unsigned no_increment = 3;
#else
constexpr unsigned no_increment = 7;
#endif

__attribute__((noinline)) void zero_trip (unsigned n)
{
  RVTT_TEST_LANE_RESET ();
  if (n & 1)
    do
      {
	__builtin_rvtt_sfppushc (0);
	__builtin_rvtt_sfppopc (0);
	auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0,
					      no_increment);
	auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31,
						 0, 0, 0);
	auto converted = __builtin_rvtt_sfpcast (shifted, 0);
	__builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0,
				 no_increment);
	__builtin_rvtt_ttincrwc (0, 2, 0, 0);
      }
    while (--n);
}
